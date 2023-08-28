#%%    
import os
import time
from _metal import ffi, lib
from shmem_demo import shmem_demo
from ipi_shmem_demo import ipi_shmem_demo
#######################################################################
# Set up RPU
#######################################################################
os.system("echo stop > /sys/class/remoteproc/remoteproc0/state")
os.system("echo RPU_libmetal_amp_demo.elf > /sys/class/remoteproc/remoteproc0/firmware")
os.system("echo start > /sys/class/remoteproc/remoteproc0/state")

try:
    retval = lib.sys_init()
    lib.metal_set_log_level(lib.METAL_LOG_DEBUG)
    ret = shmem_demo()
    if ret:
        raise RuntimeError("shared memory demo failed.")
    time.sleep(1.0)
    ret = lib.atomic_shmem_demo()
    if ret:
        raise RuntimeError("shared memory atomic demo failed.")
    time.sleep(1.0)
    ret = ipi_shmem_demo()
    if ret:
        raise RuntimeError("IPI and shared memory demo failed.")
    time.sleep(1.0)

finally:
    lib.sys_cleanup()

# %%
