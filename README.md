dmamem is a linux driver for allocating dma memory in a no-map region, and getting a virtual pointer to that memory from userspace.

The driver has a char device which is accessible through /dev/dmamem after the driver has been probed. 
The compatibility string is "dmamem". Use mmap after the device has been opened to get the virutal pointer.


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

 
  open char device in /dev/dmamem
  mmap has been overloaded
 
  if audoprobing is not desired with compatibility string, bind the driver manually:
    echo dmamem > /sys/bus/platform/devices/0.dmamem/driver_override
    echo 0.dmamem > /sys/bus/platform/drivers/dmamem/bind
*This is just and example, inspect /sys/ on the specific platform 
