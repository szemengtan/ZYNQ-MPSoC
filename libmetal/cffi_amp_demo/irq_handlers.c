#include <stdio.h>
#include <metal/atomic.h>
#include <metal/irq.h>
#include "common.h"

static atomic_flag *remote_nkicked_ptr = NULL; /* is remote kicked, 0 - kicked, 1 - not-kicked */

static int ipi_irq_handler (int vect_id, void *priv)
{
	(void)vect_id;
	(void)priv;
	if (remote_nkicked_ptr != NULL) atomic_flag_clear(remote_nkicked_ptr);
	return METAL_IRQ_HANDLED;
}

void ipi_kick_register_handler_shmem_demo(void)
{
    ipi_kick_register_handler(ipi_irq_handler, NULL);
}

void set_remote_nkicked_ptr(atomic_flag *ptr)
{
	remote_nkicked_ptr = ptr;
}
