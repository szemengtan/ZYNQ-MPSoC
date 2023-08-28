#%%
from _metal import ffi, lib
import ctypes
import os
import time

CONFIG_IPI_DEV_NAME = b"ff340000.ipi"
CONFIG_TTC_DEV_NAME = b"ff110000.timer"
CONFIG_IPI_MASK = 0x100

BUS_NAME = b"platform"
IPI_DEV_NAME = CONFIG_IPI_DEV_NAME
SHM_DEV_NAME = b"3ed80000.shm"
TTC_DEV_NAME = CONFIG_TTC_DEV_NAME

# Shared memory offsets
SHM_DEMO_CNTRL_OFFSET = 0x0
SHM_TX_AVAIL_OFFSET = 0x04
SHM_RX_AVAIL_OFFSET = 0x0C
SHM_TX_BUFFER_OFFSET = 0x14
SHM_RX_BUFFER_OFFSET = 0x800

SHM_BUFFER_SIZE = 0x400

DEMO_STATUS_IDLE = 0x0
DEMO_STATUS_START = 0x1 # Status value to indicate demo start

TEST_MSG = b"Hello World - libmetal shared memory demo"

class MsgHdr(ctypes.Structure):
    _fields_ = [("index", ctypes.c_uint32), 
                ("len", ctypes.c_uint32)]

def shmem_echo(shm_io):
    tx_count = 0
    rx_count = 0
    tx_data = ffi.NULL
    rx_data = ffi.NULL
    try:
        print("Setting up shared memory demo")
        # Clear demo status value
        lib.metal_io_write32(shm_io, SHM_DEMO_CNTRL_OFFSET, 0)
        # Clear TX/RX available
        lib.metal_io_write32(shm_io, SHM_TX_AVAIL_OFFSET, 0)
        lib.metal_io_write32(shm_io, SHM_RX_AVAIL_OFFSET, 0)

        print("Starting shared memory demo.")
        # Notify the remote the demo starts
        lib.metal_io_write32(shm_io, SHM_DEMO_CNTRL_OFFSET, DEMO_STATUS_START)
        
        # preparing data to send
        data_len = ctypes.sizeof(MsgHdr) + len(TEST_MSG) + 1
        tx_data = lib.metal_allocate_memory(data_len)
        # Use uintptr_t to get a quantity large enough to store an address
        print(f"Allocate memory returns {ffi.cast('intptr_t', tx_data) = }")
        addr = int(ffi.cast('uintptr_t', tx_data))
        if addr == 0:
            raise RuntimeError("Failed to allocate TX memory")
        msg_hdr = MsgHdr.from_address(addr)
        msg_hdr.index = tx_count
        msg_hdr.len = len(TEST_MSG) + 1
        # Write message immediately after the header
        cp = (ctypes.c_char*(msg_hdr.len)).from_address(addr + ctypes.sizeof(MsgHdr))
        cp.value = TEST_MSG
        print(f"Message sent is {cp.value}")
        # Write data to the shared memory
        ret = lib.metal_io_block_write(shm_io, SHM_TX_BUFFER_OFFSET, tx_data, data_len)
        if ret < 0:
            raise RuntimeError("Unable to metal_io_block_write")
        # Increase the number of buffers available to notify the remotr
        tx_count += 1
        lib.metal_io_write32(shm_io, SHM_TX_AVAIL_OFFSET, tx_count)
        # Wait for remote to echo back the data
        while lib.metal_io_read32(shm_io, SHM_RX_AVAIL_OFFSET) == rx_count:
            pass
        rx_count += 1
        # New received data are available, allocate buffer to receive data
        rx_data = lib.metal_allocate_memory(data_len)
        addr = int(ffi.cast('uintptr_t', rx_data))
        if addr == 0:
            raise RuntimeError("Failed to allocate RX memory")
        # Read data from the shared memory
        ret = lib.metal_io_block_read(shm_io, SHM_RX_BUFFER_OFFSET, rx_data, data_len)
        if ret < 0:
            raise RuntimeError("Unable to metal_io_block_read")
        # Verify the received data
        ret = lib.memcmp(tx_data, rx_data, data_len)
        if ret != 0:
            print("Receive data verification failed.")
            print("Expected: ")
            lib.dump_buffer(tx_data, data_len)
            print("Actual: ")
            lib.dump_buffer(rx_data, data_len)
        else:
            dp =(ctypes.c_char*(data_len - ctypes.sizeof(MsgHdr))).from_address(addr + ctypes.sizeof(MsgHdr))
            print(f"Message received is {dp.value}")
        # Notify the remote the demo has finished.
        lib.metal_io_write32(shm_io, SHM_DEMO_CNTRL_OFFSET, DEMO_STATUS_IDLE)
    finally:
        if tx_data != ffi.NULL:
            lib.metal_free_memory(tx_data);
        if rx_data != ffi.NULL:
            lib.metal_free_memory(rx_data);


def shmem_demo():
    lib.print_demo(b"shared memory")
    device_ptr = ffi.new("struct metal_device **")
    region_ptr = ffi.new("struct metal_io_region *")
    ret = lib.metal_device_open(BUS_NAME, SHM_DEV_NAME, device_ptr)
    print(f"Open device {SHM_DEV_NAME}, {ret = }")
    io = lib.metal_device_io_region(device_ptr[0], 0)
    name = ffi.string(device_ptr[0].name)
    print(f"IO region for {name}, {io = }")
    ret = shmem_echo(io)
    print("Closing the device")
    lib.metal_device_close(device_ptr[0])
    return ret
