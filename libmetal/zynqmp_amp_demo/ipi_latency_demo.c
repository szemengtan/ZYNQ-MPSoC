/*
 * Copyright (c) 2017, Xilinx Inc. and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*****************************************************************************
 * ipi_latency_demo.c
 * This demo measures the IPI latency between the APU and RPU.
 * This demo does the follwing steps:
 *
 *  1. Get the shared memory device I/O region.
 *  1. Get the TTC timer device I/O region.
 *  2. Get the IPI device I/O region.
 *  3. Register IPI interrupt handler.
 *  4. Write to shared memory to indicate demo starts
 *  5. Reset the APU to RPU TTC counter and then kick IPI to notify the
 *     remote.
 *  6. When it receives IPI interrupt, the IPI interrupt handler to stop
 *     the RPU to APU TTC counter.
 *  7. Accumulate APU to RPU and RPU to APU counter values.
 *  8. Repeat step 5, 6 and 7 for 1000 times
 *  9. Write shared memory to indicate RPU about demo finishes and kick
 *     IPI to notify.
 * 10. Clean up: disable IPI interrupt, deregister the IPI interrupt handler.
 */

#include <unistd.h>
#include <stdio.h>
#include <metal/errno.h>
#include <sys/types.h>
#include <metal/atomic.h>
#include <metal/io.h>
#include <metal/device.h>
#include <metal/irq.h>
#include <metal/time.h>
#include "common.h"

#define TTC_CNT_APU_TO_RPU 2 /* APU to RPU TTC counter ID */
#define TTC_CNT_RPU_TO_APU 3 /* RPU to APU TTC counter ID */

#define TTC_CLK_FREQ_HZ	100000000
#define NS_PER_SEC     1000000000
#define NS_PER_TTC_TICK (NS_PER_SEC / TTC_CLK_FREQ_HZ)

/* Shared memory offset */
#define SHM_DEMO_CNTRL_OFFSET    0x0

#define DEMO_STATUS_IDLE         0x0
#define DEMO_STATUS_START        0x1 /* Status value to indicate demo start */

#define ITERATIONS 1000

struct channel_s {
	struct metal_device *shm_dev; /* Shared memory metal device */
	struct metal_io_region *shm_io; /* Shared memory metal i/o region */
	struct metal_device *ttc_dev; /* TTC metal device */
	struct metal_io_region *ttc_io; /* TTC metal i/o region */
	atomic_flag remote_nkicked; /* 0 - kicked from remote */
};

/**
 * @brief read_timer() - return TTC counter value
 *
 * @param[in] ttc_io - TTC timer i/o region
 * @param[in] cnt_id - counter ID
 */
static inline uint32_t read_timer(struct metal_io_region *ttc_io,
				unsigned long cnt_id)
{
	unsigned long offset = XTTCPS_CNT_VAL_OFFSET +
				XTTCPS_CNT_OFFSET(cnt_id);

	return metal_io_read32(ttc_io, offset);
}

/**
 * @brief reset_timer() - function to reset TTC counter
 *        Set the RST bit in the Count Control Reg.
 *
 * @param[in] ttc_io - TTC timer i/o region
 * @param[in] cnt_id - counter id
 */
static inline void reset_timer(struct metal_io_region *ttc_io,
				unsigned long cnt_id)
{
	uint32_t val;
	unsigned long offset = XTTCPS_CNT_CNTRL_OFFSET +
				XTTCPS_CNT_OFFSET(cnt_id);

	val = XTTCPS_CNT_CNTRL_RST_MASK;
	metal_io_write32(ttc_io, offset, val);
}

/**
 * @brief stop_timer() - function to stop TTC counter
 *        Set the disable bit in the Count Control Reg.
 *
 * @param[in] ttc_io - TTC timer i/o region
 * @param[in] cnt_id - counter id
 */
static inline void stop_timer(struct metal_io_region *ttc_io,
				unsigned long cnt_id)
{
	uint32_t val;
	unsigned long offset = XTTCPS_CNT_CNTRL_OFFSET +
				XTTCPS_CNT_OFFSET(cnt_id);

	val = XTTCPS_CNT_CNTRL_DIS_MASK;
	metal_io_write32(ttc_io, offset, val);
}

/**
 * @brief ipi_irq_handler() - IPI interrupt handler
 *        It will clear the notified flag to mark it's got an IPI interrupt.
 *        It will stop the RPU->APU timer and will clear the notified
 *        flag to mark it's got an IPI interrupt
 *
 * @param[in] vect_id - IPI interrupt vector ID
 * @param[in/out] priv - communication channel data for this application.
 *
 * @return - If the IPI interrupt is triggered by its remote, it returns
 *           METAL_IRQ_HANDLED. It returns METAL_IRQ_NOT_HANDLED, if it is
 *           not the interrupt it expected.
 *
 */
static int ipi_irq_handler (int vect_id, void *priv)
{
	struct channel_s *ch = (struct channel_s *)priv;

	(void)vect_id;

	if (ch) {
		/* stop RPU -> APU timer */
		stop_timer(ch->ttc_io, TTC_CNT_RPU_TO_APU);
		atomic_flag_clear(&ch->remote_nkicked);
		return METAL_IRQ_HANDLED;
	}
	return METAL_IRQ_NOT_HANDLED;
}

/**
 * @brief ttc_vs_clock_gettime() sanity check: TTC and CLOCK_MONOTONIC
 *	  Compare TTC counts with the CLOCK_MONOTONIC over sleep(1).
 *	  They should be very close, e.g. within 6 us for 100 MHz TTC
 *
 * @param[in] ch - channel information for the ttc timer
 */

static void ttc_vs_clock_gettime(struct channel_s *ch)
{
	uint64_t ttc, lnx = metal_get_timestamp();

	reset_timer(ch->ttc_io, TTC_CNT_APU_TO_RPU);
	sleep(1);
	stop_timer(ch->ttc_io, TTC_CNT_APU_TO_RPU);
	lnx = metal_get_timestamp() - lnx;
	ttc = NS_PER_TTC_TICK * read_timer(ch->ttc_io, TTC_CNT_APU_TO_RPU);
	LPRINTF("sleep(1) check: TTC= %lu / CLOCK_MONOTONIC= %lu = %.2f\n",
		ttc, lnx, lnx ? (ttc/(float)lnx) : 0);
}

/**
 * @brief measure_ipi_latency() - Measure latency of IPI
 *        Repeatedly kick IPI to notify the remote and then wait for IPI kick
 *        from RPU and measure the latency. Similarly, measure the latency
 *        from RPU to APU. Each iteration, record this latency and after the
 *        loop has finished, report the total latency in nanseconds.
 *        Notes:
 *        - RPU will repeatedly wait for IPI from APU until APU
 *          notifies remote demo has finished by setting the value in the
 *          shared memory.
 *
 * @param[in] ch - channel information, which contains the IPI i/o region,
 *                 shared memory i/o region and the ttc timer i/o region.
 * @return - 0 on success, error code if failure.
 */
static int measure_ipi_latency(struct channel_s *ch)
{
	struct metal_stat a2r = STAT_INIT;
	struct metal_stat r2a = STAT_INIT;
	uint64_t delta_ns;
	int i;

	LPRINTF("Starting IPI latency task\n");
	ttc_vs_clock_gettime(ch);
	/* write to shared memory to indicate demo has started */
	metal_io_write32(ch->shm_io, SHM_DEMO_CNTRL_OFFSET, DEMO_STATUS_START);

	delta_ns = metal_get_timestamp();
	for ( i = 1; i <= ITERATIONS; i++) {
		/* Reset TTC counter */
		reset_timer(ch->ttc_io, TTC_CNT_APU_TO_RPU);
		/* Kick IPI to notify the remote */
		kick_ipi(NULL);
		/* irq handler stops timer for rpu->apu irq */
		wait_for_notified(&ch->remote_nkicked);

		update_stat(&a2r, read_timer(ch->ttc_io, TTC_CNT_APU_TO_RPU));
		update_stat(&r2a, read_timer(ch->ttc_io, TTC_CNT_RPU_TO_APU));
	}
	delta_ns = metal_get_timestamp() - delta_ns;

	/* write to shared memory to indicate demo has finished */
	metal_io_write32(ch->shm_io, SHM_DEMO_CNTRL_OFFSET, 0);
	/* Kick IPI to notify the remote */
	kick_ipi(NULL);

	/* report avg latencies */
	LPRINTF("IPI latency: %i iterations took %lu ns (CLOCK_MONOTONIC)\n",
		ITERATIONS, delta_ns);
	LPRINTF("TTC [min,max] are in TTC ticks: %d ns per tick\n",
		NS_PER_TTC_TICK);
	LPRINTF("APU to RPU: [%lu, %lu] avg: %lu ns\n",
		a2r.st_min, a2r.st_max,
		a2r.st_sum * NS_PER_TTC_TICK / ITERATIONS);
	LPRINTF("RPU to APU: [%lu, %lu] avg: %lu ns\n",
		r2a.st_min, r2a.st_max,
		r2a.st_sum * NS_PER_TTC_TICK / ITERATIONS);
	LPRINTF("Finished IPI latency task\n");
	return 0;
}

int ipi_latency_demo()
{
	struct metal_device *dev;
	struct metal_io_region *io;
	struct channel_s ch;
	int ret = 0;

	print_demo("IPI latency");
	memset(&ch, 0, sizeof(ch));

	/* Open shared memory device */
	ret = metal_device_open(APU_BUS_NAME, APU_SHM_DEV_NAME, &dev);
	if (ret) {
		LPERROR("Failed to open device %s.\n", APU_SHM_DEV_NAME);
		goto out;
	}

	/* Get shared memory device IO region */
	io = metal_device_io_region(dev, 0);
	if (!io) {
		LPERROR("Failed to map io region for %s.\n", dev->name);
		ret = -ENODEV;
		goto out;
	}
	ch.shm_dev = dev;
	ch.shm_io = io;

	/* Open TTC device */
	ret = metal_device_open(APU_BUS_NAME, TTC_DEV_NAME, &dev);
	if (ret) {
		LPERROR("Failed to open device %s.\n", TTC_DEV_NAME);
		goto out;
	}

	/* Get TTC IO region */
	io = metal_device_io_region(dev, 0);
	if (!io) {
		LPERROR("Failed to map io region for %s.\n", dev->name);
		ret = -ENODEV;
		goto out;
	}
	ch.ttc_dev = dev;
	ch.ttc_io = io;

	/* initialize remote_nkicked */
	atomic_flag_clear(&ch.remote_nkicked);
	atomic_flag_test_and_set(&ch.remote_nkicked);

	ret = init_ipi();
	if (ret) {
		goto out;
	}
	ipi_kick_register_handler(ipi_irq_handler, &ch);
	enable_ipi_kick();

	/* Run atomic operation demo */
	ret = measure_ipi_latency(&ch);

	/* disable IPI interrupt */
	disable_ipi_kick();
	deinit_ipi();

out:
	if (ch.ttc_dev)
		metal_device_close(ch.ttc_dev);
	if (ch.shm_dev)
		metal_device_close(ch.shm_dev);
	return ret;

}

