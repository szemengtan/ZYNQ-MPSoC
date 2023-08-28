/*
 * Copyright (c) 2017, Xilinx Inc. and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

 /*****************************************************************************
 * atomic_shmem_demod.c - Shared memory atomic operation demo
 * This demo will:
 *
 *  1. Open the shared memory device.
 *  2. Open the IPI device.
 *  3. Register IPI interrupt handler.
 *  4. Kick IPI to notify the other end to start the demo
 *  5. Start atomic add by 1 for 5000 times over the shared memory
 *  6. Wait for remote IPI kick to know when the remote has finished the demo.
 *  7. Verify the result. As two sides both have done 5000 times of adding 1,
 *     check if the end result is 5000*2.
 *  8. Clean up: deregister the IPI interrupt handler, close the IPI device
 *     , close the shared memory device.
 */

#include <unistd.h>
#include <stdio.h>
#include <metal/atomic.h>
#include <metal/cpu.h>
#include <metal/io.h>
#include <sys/types.h>
#include <metal/device.h>
#include <metal/irq.h>
#include <metal/errno.h>
#include <time.h>
#include "common.h"

#define ATOMIC_INT_OFFSET 0x0 /* shared memory offset for atomic operation */
#define ITERATIONS 5000

static atomic_flag remote_nkicked; /* is remote kicked, 0 - kicked,
				       1 - not-kicked */

static int ipi_irq_handler (int vect_id, void *priv)
{
	(void)vect_id;
	(void)priv;
	atomic_flag_clear(&remote_nkicked);
	return METAL_IRQ_HANDLED;
}

/**
 * @brief   atomic_add_shmem() - Shared memory atomic operation demo
 *          This task will:
 *          * Write to shared memory to notify the remote to start atomic add on
 *            the shared memory for 1000 times.
 *          * Start atomic add by 1 for 5000 times to first 32 bits of memory in
 *            the shared memory which is pointed to by shm_io.
 *          * Wait for the remote to write to shared memory
 *          * Once it received the polling kick from the remote, it will check
 *            if the value stored in the shared memory is the same as the
 *            expected.
 *          * It will print if the atomic add test has passed or not.
 * @param[in] shm_io - shared memory metal i/o region
 * @return - If setup failed, return the corresponding error number. Otherwise
 *          return 0 on success.
 */
static int atomic_add_shmem(struct metal_io_region *shm_io)
{
	int i, ret;
	atomic_int *shm_int;

	LPRINTF("Starting atomic shared memory task.\n");

	/* Initialize the shared memory on which we run the atomic add */
	shm_int = (atomic_int *)metal_io_virt(shm_io,
					ATOMIC_INT_OFFSET);
	atomic_store(shm_int, 0);

	/* Kick the remote to notify demo starts. */
	kick_ipi(NULL);

	/* Do atomic add over the shared memory */
	for (i = 0; i < ITERATIONS; i++) {
		atomic_fetch_add(shm_int, 1);
	}

	/* Wait for kick from RPU to know when RPU finishes the demo */
	wait_for_notified(&remote_nkicked);

	if (atomic_load(shm_int) == (ITERATIONS << 1 )) {
		LPRINTF("shm atomic demo PASSED!\n");
		ret = 0;
	} else {
		LPRINTF("shm atomic demo FAILED. expected: %u, actual: %u\n",
			(unsigned int)(ITERATIONS << 1), atomic_load(shm_int));
		ret = -1;
	}

	return ret;
}

int atomic_shmem_demo()
{
	struct metal_device *shm_dev = NULL;
	struct metal_io_region *shm_io = NULL;
	int ret = 0;

	print_demo("atomic operation over shared memory");

	/* Open shared memory device */
	ret = metal_device_open(APU_BUS_NAME, APU_SHM_DEV_NAME, &shm_dev);
	if (ret) {
		LPERROR("Failed to open device %s.\n", APU_SHM_DEV_NAME);
		goto out;
	}

	/* Get shared memory device IO region */
	shm_io = metal_device_io_region(shm_dev, 0);
	if (!shm_io) {
		LPERROR("Failed to map io region for %s.\n", shm_dev->name);
		ret = -ENODEV;
		goto out;
	}

	/* initialize remote_nkicked */
	atomic_flag_clear(&remote_nkicked);
	atomic_flag_test_and_set(&remote_nkicked);

	ret = init_ipi();
	if (ret) {
		goto out;
	}
	ipi_kick_register_handler(ipi_irq_handler, NULL);
	enable_ipi_kick();

	/* Run atomic operation demo */
	ret = atomic_add_shmem(shm_io);

	/* disable IPI interrupt */
	disable_ipi_kick();
	deinit_ipi();

out:
	if (shm_dev)
		metal_device_close(shm_dev);
	return ret;

}
