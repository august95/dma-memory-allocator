dmamem is a linux driver for allocating dma memory, and getting a virtual pointer into no-map memory. 

The driver has a char device which is accessible through /dev/dmamem after the driver has been probed. 
The compatibility string is "dmamem". Use mmap after the device has been opened to get the virutal pointer.
