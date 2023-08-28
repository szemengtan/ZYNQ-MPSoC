/*
 * Copyright (c) 2012 Xilinx, Inc.  All rights reserved.
 *
 * Xilinx, Inc.
 * XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS" AS A
 * COURTESY TO YOU.  BY PROVIDING THIS DESIGN, CODE, OR INFORMATION AS
 * ONE POSSIBLE   IMPLEMENTATION OF THIS FEATURE, APPLICATION OR
 * STANDARD, XILINX IS MAKING NO REPRESENTATION THAT THIS IMPLEMENTATION
 * IS FREE FROM ANY CLAIMS OF INFRINGEMENT, AND YOU ARE RESPONSIBLE
 * FOR OBTAINING ANY RIGHTS YOU MAY REQUIRE FOR YOUR IMPLEMENTATION.
 * XILINX EXPRESSLY DISCLAIMS ANY WARRANTY WHATSOEVER WITH RESPECT TO
 * THE ADEQUACY OF THE IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO
 * ANY WARRANTIES OR REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE
 * FROM CLAIMS OF INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#include <stdio.h>
#include <unistd.h>
#include "xil_types.h"
#include <metal/io.h>
#include <metal/device.h>
#include "common.h"
#include "sys_init.h"

int main()
{
    int ret = 0;
    struct metal_device *device = NULL;
    struct metal_io_region *io = NULL;

    ret = sys_init();
    if (ret) {
        LPERROR("Failed to initialize system.\n");
        goto out;
    }
    
    // Open the shared memory device
    ret = metal_device_open(APU_BUS_NAME, APU_SHM_DEV_NAME, &device);
    if (ret) {
        LPERROR("Failed to open device %s.\n", APU_SHM_DEV_NAME);
        goto out;
    }

    // Get shared memory device IO region
    io = metal_device_io_region(device, 0);
    if (!io) {
        LPERROR("Failed to get region for %s.\n", device->name);
        ret = -ENODEV;
        goto out;
    }

    /*
    ret = metal_shmem_open(device->name, 16777216, &io);
    if (ret) {
        LPERROR("Failed to get region for %s.\n", device->name);
        goto out;
    }
    */
   
    // Clear shared memory
    LPRINTF("Shared memory region size: %ld\r\n", metal_io_region_size(io));
    metal_io_block_set(io, 0, 0, metal_io_region_size(io));
    // Display current value of specific location 
    int offset = 20;
    LPRINTF("Value at offset %d is %ld\r\n", offset, metal_io_read32(io, offset));
    // Write to the location 
    metal_io_write32(io, offset, 0xdeadbeef);
    // Read the data back
    LPRINTF("After setting, value at offset %d is 0x%lx\r\n", offset, metal_io_read32(io, offset));
    // Get a virtual address pointer to the location
    u32 *ptr = (u32 *) metal_io_virt(io, offset);
    // Read the data back
    LPRINTF("Getting the data via a pointer, the result is 0x%x\r\n", *ptr);
    // Display the physical address
    LPRINTF("Physical address is 0x%lx\r\n", metal_io_phys(io, offset));

    printf("Hello World\n");

out:
    sys_cleanup();
    return ret;
}
