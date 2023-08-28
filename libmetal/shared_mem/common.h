/*
 * Copyright (c) 2017, Xilinx Inc. and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __COMMON_H__
#define __COMMON_H__

#include <sys/types.h>
#include <metal/irq.h>
#include <metal/cpu.h>
#include <stdio.h>

#define BUS_NAME        "platform"
#define SHM_DEV_NAME    "3ed80000.shm"

#define LPRINTF(format, ...) \
  printf("CLIENT> " format, ##__VA_ARGS__)

#define LPERROR(format, ...) LPRINTF("ERROR: " format, ##__VA_ARGS__)

/**
 * @brief dump_buffer() - print hex value of each byte in the buffer
 *
 * @param[in] buf - pointer to the buffer
 * @param[in] len - len of the buffer
 */
static inline void dump_buffer(void *buf, unsigned int len)
{
	unsigned int i;
	unsigned char *tmp = (unsigned char *)buf;

	for (i = 0; i < len; i++) {
		printf(" %02x", *(tmp++));
		if (!(i % 20))
			printf("\n");
	}
	printf("\n");
}

#endif /* __COMMON_H__ */
