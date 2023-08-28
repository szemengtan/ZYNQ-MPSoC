from cffi import FFI
ffibuilder = FFI()
from io import StringIO

with open("metal.cdef") as fp:
    ffibuilder.cdef(fp.read())

ffibuilder.set_source("_metal",
r"""
    #include <metal/errno.h>
    #include <metal/alloc.h>
    #include <metal/log.h>
    #include <metal/list.h>
    #include <metal/atomic.h>
    #include <metal/cpu.h>
    #include <metal/io.h>
    #include <sys/types.h>
    #include <metal/device.h>
    #include <metal/irq.h>
    #include <metal/errno.h>
    #include "sys_init.h"
    #include "irq_handlers.h"
    #include "common.h"
""", 
    sources = ["sys_init.c", "irq_handlers.c", "ipi-uio.c", "shmem_atomic_demo.c"],
    libraries = ["metal"])

if __name__ == "__main__":
    ffibuilder.compile(verbose=True)