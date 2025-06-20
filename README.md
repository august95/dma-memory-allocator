dmamem is a linux driver for allocating dma memory in a no-map region, and getting a virtual pointer to that memory from userspace.

The driver has a char device which is accessible through /dev/dmamem after the driver has been probed. 
The compatibility string is "dmamem". Use mmap after the device has been opened to get the virutal pointer.
