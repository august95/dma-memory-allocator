#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>

/*
  allocates DMA buffer on reserved no-map memory region in device-tree

  1. add to defconfig:
  CONFIG_DMAMEM_DRIVER=y

  2. add drivers/dmamem/Kconfig file:

  config DMAMEM_DRIVER
    tristate "DMAMEM Reserved Memory Driver"
    depends on OF
    help
      Say Y to enable the DMAMEM reserved memory driver.

  3. Source file in drivers/Kconfig:

  source "drivers/dmamem/Kconfig"

  4. add file to drivers/Makefile:
  obj-$(CONFIG_DMAMEM_DRIVER) += dmamem/

  Device-tree changes:

  reserved-memory {
    #address-cells = < 0x0 >;
    #size-cells = < 0x0 >;
    ranges = <>;

      bank0@0 {
      no-map;
      compatible = "shared-dma-pool";
      phandle = < 0x1 >;
      reg = < 0x80000000 0x20000000 >;
    };

  };
  reserved-driver@0 {
    compatible = "dmamem";
    memory-region = < 0x1>;
  };

 *
 * open char device in /dev/dmamem
 * mmap has been overloaded
 *
 * if audoprobing is not desired with compatibility string, bind the driver manually:
 *  echo dmamem > /sys/bus/platform/devices/0.dmamem/driver_override
 *  echo 0.dmamem > /sys/bus/platform/drivers/dmamem/bind
 *  *This is just and example, inspect /sys/ on the specific platform
 */


#define DEVICE_NAME "dmamem"
#define CLASS_NAME  "dmamem_class"

static dev_t dev_num;
static struct class *cclass;
static struct cdev cdev;
static struct device *cdevice;
static struct device *pdevice;

static struct {
	void __iomem *virt_base;
	phys_addr_t phys_base;
	u64 size;
} dmamem;

static int dmamem_mmap(struct file *filp, struct vm_area_struct *vma)
{
  size_t size = vma->vm_end - vma->vm_start;
  if (size > dmamem.size)
      return -EINVAL;

  vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
  return remap_pfn_range(vma, vma->vm_start, dmamem.phys_base >> PAGE_SHIFT, size, vma->vm_page_prot);
}

static int dmamem_open(struct inode *inode, struct file *file) {
  return 0;
}

static int dmamem_release(struct inode *inode, struct file *file) {
  return 0;
}

static ssize_t dmamem_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
  const char *msg = "dmamem platform char device read!\n";
  size_t len = strlen(msg);
  if (*ppos >= len)
      return 0;
  if (copy_to_user(buf, msg, len))
      return -EFAULT;
  *ppos += len;
  return len;
}

static struct file_operations dmamem_fops = {
  .owner   = THIS_MODULE,
  .open    = dmamem_open,
  .release = dmamem_release,
  .read    = dmamem_read,
  .mmap    = dmamem_mmap
};

static int of_property_read_u64_index_(const struct device_node *np, const char *propname, u64 *out_value, u32 u64_index);

static int dmamem_probe(struct platform_device *pdev)
{
  int ret;
  pdevice = &pdev->dev;

  //create a char device that is accessible through user space in /dev/DEVICE_NAME
  ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
  if (ret)
      return ret;

  //initialize and add the char device with file operation overloads
  cdev_init(&cdev, &dmamem_fops);
  ret = cdev_add(&cdev, dev_num, 1);
  if (ret)
  {
    dev_err(pdevice, "cdev add failed\n");
  	return ret;
  }

  //create a device class and device node
  cclass = class_create(CLASS_NAME);
  if (IS_ERR(cclass)) {
      ret = PTR_ERR(cclass);
      dev_err(pdevice, "class create failed\n");
      return ret;
  }

  //register the platform driver (reserved-driver@0) as parent for the char device
  //we want the platform driver to be accessible through /dev in userspace
  cdevice = device_create(cclass, pdevice, dev_num, NULL, DEVICE_NAME);
  if (IS_ERR(cdevice)) {
      ret = PTR_ERR(cdevice);
      dev_err(pdevice, " device create failed\n");
      return ret;
  }

  int rc  = of_reserved_mem_device_init(pdevice);
  if(rc) {
    dev_err(pdevice, "Could not get reserved memory\n");
    return ret;
  }

  //from the platform driver (reserved-driver@0) get the reserved memory in the device tree (bank0@0)
  struct device_node *reserved_mem_node = 0;
  reserved_mem_node = of_parse_phandle(pdevice->of_node, "memory-region", 0);

  //read the buffer length from (bank0@0)
  const char* of_value = "reg";
  if(of_property_read_u64_index_(reserved_mem_node, of_value, &dmamem.size, 1))
  {
    dev_err(pdevice, "getting 'reg' field from reserved-driver node in device tree failed\n");
    return ret;
  }

  dma_set_coherent_mask(pdevice,  DMA_BIT_MASK(32));
  dma_alloc_coherent(pdevice, (size_t)dmamem.size,  &dmamem.phys_base, GFP_KERNEL);
  dev_info(pdevice, "Allocated memory, vaddr: 0x%0llX, paddr: 0x%0llX\n", (u64)dmamem.virt_base, dmamem.phys_base);

  return 0;
}


static int of_property_read_u64_index_(const struct device_node *np, const char *propname, u64 *out_value, u32 u64_index)
{
/*
 * 64 bit values from the device tree is made out of two 32 bit values
 * reg =  < 0x0 0x80000000 0x0 0x20000000 >;
 *        | index 0  | index 1|
 */
  u32 hi, lo;
  for (int i = u64_index*2; i < (u64_index*2)+2; i++) {
    if (!of_property_read_u32_index(np, propname, i, &hi) &&
      !of_property_read_u32_index(np, propname, i + 1, &lo)) {
      *out_value = ((u64)hi << 32) | lo;
      return 0;
    }
  }
  return -1;
}

static int dmamem_remove(struct platform_device *pdev) {
  device_destroy(cclass, dev_num);
  class_destroy(cclass);
  cdev_del(&cdev);
  unregister_chrdev_region(dev_num, 1);
  return 0;
}

MODULE_DEVICE_TABLE(of, dmamem_of_match);

// Device tree match table
static const struct of_device_id dmamem_of_match[] = {
  { .compatible = "dmamem" },
  { /* sentinel */ }
};

static struct platform_driver dmamem_driver = {
  .driver = {
      .name = DEVICE_NAME,
      .of_match_table = dmamem_of_match,
  },
  .probe  = dmamem_probe,
  .remove = dmamem_remove
};

MODULE_LICENSE("GPL");
MODULE_AUTHOR("You");
MODULE_DESCRIPTION("Minimal mmap reserved memory driver");

module_platform_driver(dmamem_driver);
