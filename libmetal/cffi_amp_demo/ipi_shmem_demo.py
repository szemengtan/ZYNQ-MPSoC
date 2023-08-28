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
SHM_DESC_OFFSET_TX = 0x0
SHM_BUFF_OFFSET_TX = 0x04000
SHM_DESC_OFFSET_RX = 0x02000
SHM_BUFF_OFFSET_RX = 0x104000

# Shared memory descriptors offset
SHM_DESC_AVAIL_OFFSET = 0x00
SHM_DESC_USED_OFFSET  = 0x04
SHM_DESC_ADDR_ARRAY_OFFSET = 0x08

PKGS_TOTAL = 1024

BUF_SIZE_MAX = 512
SHUTDOWN = b"shutdown"

NS_PER_S = (1000 * 1000 * 1000)

class MsgHdr(ctypes.Structure):
    _fields_ = [("index", ctypes.c_uint32), 
                ("len", ctypes.c_uint32)]

def get_timestamp():
    return time.monotonic_ns()

def ipi_shmem_echo(shm_io, remote_nkicked_ptr):
    """This task will:
        * Get the timestamp and put it into the ping shared memory
        * Update the shared memory descriptor for the new available
            ping buffer.
        * Trigger IPI to notifty the remote.
        * Repeat the above steps until it sends out all the packages.
        * Monitor IPI interrupt, verify every received package.
        * After all the packages are received, it sends out shutdown
            message to the remote.
    """
    tx_buff = ffi.NULL
    rx_buff = ffi.NULL
    try:
        tx_buff = lib.metal_allocate_memory(BUF_SIZE_MAX)
        if tx_buff == ffi.NULL:
            raise RuntimeError("Failed to allocate local tx buffer for msg")
        rx_buff = lib.metal_allocate_memory(BUF_SIZE_MAX)
        if rx_buff == ffi.NULL:
            raise RuntimeError("Failed to allocate local rx buffer for msg")
        # Clear shared memory
        print(f"Size of shared memory {shm_io[0].size = }")
        lib.metal_io_block_set(shm_io, 0, 0, lib.metal_io_region_size(shm_io));

        # Set the offsets into the shared memory for TX and RX buffers
        # Here is the Shared memory structure of this demo:
        # SHM_DESC_OFFSET_TX    
        # AVAIL_OFFSET      |0x0   - 0x03        | number of APU to RPU buffers available to RPU |
        # USED_OFFSET       |0x04  - 0x07        | number of APU to RPU buffers consumed by RPU |
        # ADDR_ARRAY_OFFSET |0x08  - 0x1FFC      | address array for shared buffers from APU to RPU |
        # SHM_DESC_OFFSET_RX
        # AVAIL_OFFSET      |0x2000 - 0x2003     | number of RPU to APU buffers available to APU |
        # USED_OFFSET       |0x2004 - 0x2007     | number of RPU to APU buffers consumed by APU |
        # ADDR_ARRAY_OFFSET |0x2008 - 0x3FFC     | address array for shared buffers from RPU to APU |
        # SHM_BUFF_OFFSET_TX
        # BUFF_OFFSET_TX    |0x04000 - 0x103FFC  | APU to RPU buffers |
        # SHM_BUFF_OFFSET_RX
        # BUFF_OFFSET_RX    |0x106000 - 0x205FFC | RPU to APU buffers |
        tx_avail_offset = SHM_DESC_OFFSET_TX + SHM_DESC_AVAIL_OFFSET
        tx_used_offset = SHM_DESC_OFFSET_TX + SHM_DESC_USED_OFFSET
        rx_avail_offset = SHM_DESC_OFFSET_RX + SHM_DESC_AVAIL_OFFSET
        rx_used_offset = SHM_DESC_OFFSET_RX + SHM_DESC_USED_OFFSET
        tx_addr_offset = SHM_DESC_OFFSET_TX + SHM_DESC_ADDR_ARRAY_OFFSET
        rx_addr_offset = SHM_DESC_OFFSET_RX + SHM_DESC_ADDR_ARRAY_OFFSET
        tx_data_offset = SHM_DESC_OFFSET_TX + SHM_BUFF_OFFSET_TX
        rx_data_offset = SHM_DESC_OFFSET_RX + SHM_BUFF_OFFSET_RX

        print("Start echo flood testing...")
        print("Sending msgs to remote...")

        tx_addr = int(ffi.cast('uintptr_t', tx_buff))
        tx_msg_hdr = MsgHdr.from_address(tx_addr)
        tx_llp = ctypes.c_longlong.from_address(tx_addr + ctypes.sizeof(MsgHdr))
        tx_cp = (ctypes.c_char*(BUF_SIZE_MAX-ctypes.sizeof(MsgHdr))).from_address(tx_addr + ctypes.sizeof(MsgHdr))

        for i in range(PKGS_TOTAL):
            # Construct a message to send
            tx_msg_hdr.index = i
            tx_msg_hdr.len = ctypes.sizeof(ctypes.c_longlong)
            tx_llp.value = tstart = get_timestamp()
            
            # Copy message to the shared buffer at tx_data_offset
            lib.metal_io_block_write(shm_io, tx_data_offset, tx_buff, ctypes.sizeof(MsgHdr) + tx_msg_hdr.len)
            
            # Write to the address array to tell the other end the buffer address
            tx_phy_addr_32 = int(ffi.cast("uint32_t", lib.metal_io_phys(shm_io, tx_data_offset)))
            lib.metal_io_write32(shm_io, tx_addr_offset, tx_phy_addr_32)

            # Advance the offsets past this message
            tx_data_offset += ctypes.sizeof(MsgHdr) + tx_msg_hdr.len
            tx_addr_offset += ctypes.sizeof(ctypes.c_uint32)

            # Increase the number of available buffers so the receipient knows something is available
            lib.metal_io_write32(shm_io, tx_avail_offset, (i+1))

            # Kick IPI to notify that data has been put to the shared buffer 
            lib.kick_ipi(ffi.NULL)

        print("Waiting for messages to echo back and verify")
        i = 0
        # Restore to start of tx area to allow for comparison
        tx_data_offset = SHM_DESC_OFFSET_TX + SHM_BUFF_OFFSET_TX
        
        rx_addr = int(ffi.cast('uintptr_t', rx_buff))
        rx_msg_hdr = MsgHdr.from_address(rx_addr)
        rx_llp = ctypes.c_longlong.from_address(rx_addr + ctypes.sizeof(MsgHdr))

        while i != PKGS_TOTAL:
            lib.wait_for_notified(remote_nkicked_ptr)
            rx_avail = lib.metal_io_read32(shm_io, rx_avail_offset)
            while i != rx_avail:
                # Received pong from other side

                # Get the buffer location from the shared memory rx address array
                rx_phy_addr_32 = lib.metal_io_read32(shm_io, rx_addr_offset)
                rx_data_offset = lib.metal_io_phys_to_offset(shm_io, ffi.cast("metal_phys_addr_t", rx_phy_addr_32))
                if rx_data_offset == -1:
                    raise RuntimeError(f"Failed to get rx [{i}] offset: 0x{rx_phy_addr_32:x}")
                rx_addr_offset += ctypes.sizeof(ctypes.c_uint32)

                # Read message header from shared memory
                lib.metal_io_block_read(shm_io, rx_data_offset, rx_buff, ctypes.sizeof(MsgHdr))
                # Check if the message header is valid
                if rx_msg_hdr.index != i:
                    raise RuntimeError(f"Wrong msg: expected: {i}, actual: {rx_msg_hdr.index}")
                if rx_msg_hdr.len != ctypes.sizeof(ctypes.c_longlong):
                    raise RuntimeError(f"Wrong msg: length invalid: expected: {ctypes.sizeof(ctypes.c_longlong)}, actual: {rx_msg_hdr.len}")
                # Read the message and header
                lib.metal_io_block_read(shm_io, rx_data_offset, rx_buff, ctypes.sizeof(MsgHdr) + rx_msg_hdr.len)
                # Increase RX count to indicate that it has consumed the received data
                lib.metal_io_write32(shm_io, rx_used_offset, (i + 1))

                # Verify the message
                # Get the tx message previously sent
                lib.metal_io_block_read(shm_io, tx_data_offset, tx_buff, ctypes.sizeof(MsgHdr) + ctypes.sizeof(ctypes.c_longlong))
                tx_data_offset += ctypes.sizeof(MsgHdr) + ctypes.sizeof(ctypes.c_longlong)
                # Compare the received and sent messages
                if rx_llp.value != tx_llp.value:
                    raise RuntimeError(f"Data [{i}] verification failed, expected: {tx_llp.value}, actual: {rx_llp.value}")
                i += 1
        
        tend = get_timestamp()
        tdiff = tend - tstart
        # Send shutdown message
        tx_msg_hdr.index = i
        tx_msg_hdr.len = len(SHUTDOWN)
        tx_cp.value = b"shutdown"
        
        # Copy message to shared buffer
        lib.metal_io_block_write(shm_io, tx_data_offset, tx_buff, ctypes.sizeof(MsgHdr) + tx_msg_hdr.len)
        tx_phy_addr_32 = int(ffi.cast("uint32_t", lib.metal_io_phys(shm_io, tx_data_offset)))
        lib.metal_io_write32(shm_io, tx_addr_offset, tx_phy_addr_32)
        lib.metal_io_write32(shm_io, tx_avail_offset, PKGS_TOTAL + 1)
        print("Kick remote to notify shutdown message sent...")
        lib.kick_ipi(ffi.NULL)

        tdiff_avg = tdiff / PKGS_TOTAL
        print(f"Total packages: {i}, time_avg: {tdiff_avg} ns")

        return 0
    finally:
        if tx_buff != ffi.NULL:
            lib.metal_free_memory(tx_buff);
        if rx_buff != ffi.NULL:
            lib.metal_free_memory(rx_buff);

def ipi_shmem_demo():
    try:
        lib.print_demo(b"IPI and shared memory")
        shm_dev_ptr = ffi.new("struct metal_device **")
        shm_io = ffi.new("struct metal_io_region *")
        ret = lib.metal_device_open(BUS_NAME, SHM_DEV_NAME, shm_dev_ptr)
        if ret !=  0:
            raise RuntimeError(f"Failed to open device {SHM_DEV_NAME}, {ret = }")
        shm_io = lib.metal_device_io_region(shm_dev_ptr[0], 0)
        if shm_io == ffi.NULL:
            raise RuntimeError(f"Failed to map io region for {ffi.string(shm_dev_ptr[0].name)}");
        remote_nkicked_ptr = ffi.new("atomic_flag *")
        lib.set_remote_nkicked_ptr(remote_nkicked_ptr)
        # initialize remote_nkicked
        lib.atomic_flag_clear(remote_nkicked_ptr)
        lib.atomic_flag_test_and_set(remote_nkicked_ptr)

        # initialize IPI handling
        ret = lib.init_ipi()
        if ret:
            raise RuntimeError(f"Failed to initialize IPI")
        lib.ipi_kick_register_handler_shmem_demo()
        lib.enable_ipi_kick()

        # Run atomic operation demo
        ret = ipi_shmem_echo(shm_io, remote_nkicked_ptr)

        # disable IPI handling
        lib.disable_ipi_kick()
        lib.deinit_ipi()
        print("After disabling IPI handling")
    finally:
        if shm_dev_ptr != ffi.NULL:
            lib.metal_device_close(shm_dev_ptr[0])
