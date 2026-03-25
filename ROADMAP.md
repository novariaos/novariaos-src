# Roadmap for NovariaOS 0.3.0

# High Priority

## NVMe Driver
- [x] NVMe controller initialization (reset, enable)
- [x] NVMe admin queue setup and management
- [x] NVMe I/O queue setup and management
- [x] MMIO register access functions
- [x] Identify namespace command
- [x] NVMe read operations (polling mode)
- [x] NVMe write operations (polling mode)
- [ ] Interrupt-based I/O (currently uses polling)
- [x] Multiple namespace support
- [x] PCI enumeration for NVMe devices

## AHCI (SATA) Driver
- [x] PCI bus scan for AHCI controller (class 01h/06h)
- [x] ABAR (BAR5) discovery and MMIO mapping
- [x] GHC AHCI enable and controller version detection
- [x] Port enumeration and device type detection
- [x] Command list and FIS receive buffer allocation (port rebase)
- [x] IDENTIFY DEVICE command (LBA48 and LBA28 sector count)
- [x] DMA read operations (READ DMA EXT, LBA48)
- [x] DMA write operations (WRITE DMA EXT, LBA48)
- [x] Block device registration (sda, sdb, ...)
- [ ] Interrupt-based I/O (currently uses polling)
- [ ] Hot-plug support (port change detection)
- [ ] ATAPI device support
- [ ] NCQ (Native Command Queuing)
- [ ] Port multiplier support
- [ ] Error recovery and port reset

## DMA subsystem (core implementation)
- [ ] Design DMA structures in kernel
     - `dma_page` struct (`phys_addr`, `size`, `refcount`, `dirty`, `backing_file`, `file_offset`)
     - `dma_manager` (array/list of pages, free list)
- [ ] Implement DMA page allocation (via BBuddy, 4096 bytes)
     - `dma_page_t* dma_alloc_page(uint32_t flags)`
- [ ] Implement DMA page deallocation
     - `void dma_free_page(dma_page_t* page)`
- [ ] Implement DMA page read from file
     - `int dma_read_from_file(dma_page_t* page, struct file* f, size_t file_offset, size_t size, size_t page_offset)`
- [ ] Implement DMA page write to file
     - `int dma_write_to_file(dma_page_t* page, struct file* f, size_t file_offset, size_t size, size_t page_offset)`
- [ ] Implement dirty page tracking
     - `void dma_mark_dirty(dma_page_t* page, size_t offset, size_t size)`
     - `bool dma_is_dirty(dma_page_t* page)`
- [ ] Implement page synchronization
     - `int dma_sync_page(dma_page_t* page)` (full sync)
- [ ] Write kernel-internal tests
     - Allocate/free pages
     - Read/write to/from files
     - Mark dirty and sync

## NVM extension (bit shifts)
- [ ] Add SHL (0x70) instruction to interpreter
- [ ] Add SHR (0x71) instruction to interpreter
- [ ] Write test programs to verify shifts

## Basic system calls (export minimal DMA)
- [ ] DMA_ALLOC (0x0F) — wrapper around `dma_alloc_page`
- [ ] DMA_FREE (0x10) — wrapper around `dma_free_page`
- [ ] Rework READ (0x03) to use DMA pages
- [ ] Rework WRITE (0x04) to use DMA pages
- [x] Remove PRINT (0x0E)
- [ ] Renumber syscalls if needed

---

# Medium Priority

## Advanced DMA features
- [ ] Implement mapping to NVM address space
     - `int dma_map_to_nvm(dma_page_t* page, uint32_t nvm_addr, struct nvm_process* proc)`
     - `int dma_unmap_from_nvm(uint32_t nvm_addr, struct nvm_process* proc)`
- [ ] Kernel-internal tests for map/unmap
- [ ] Implement partial sync
     - `int dma_sync_range(dma_page_t* page, size_t offset, size_t size)`

## Advanced DMA system calls
- [ ] DMA_MAP (0x15) — wrapper around `dma_map_to_nvm`
- [ ] DMA_UNMAP (0x16) — wrapper around `dma_unmap_from_nvm`
- [ ] DMA_SYNC (0x11) — wrapper around `dma_sync_page`
- [ ] DMA_MSYNC (0x17) — wrapper around `dma_sync_range`

## Finish /dev/tty
- [x] Add write option
- [ ] Add read option

## Clocks!
- [X] Initialize RTC(Real Time Clock)
- [X] Implement `/dev/time`
     - [X] DevFS integration
- [X] Initialize APIC
- [X] Program APIC timer
- [X] Implement `/proc/uptime` (seconds since boot)
     - [X] Internal function: `uint64_t get_uptime(void)`
- [ ] MSI-X support (requires APIC)
     - PCI MSI-X capability discovery
     - MSI-X table setup and vector allocation
     - Route NVMe interrupt to IDT vector
- [ ] NVMe interrupt-driven I/O (requires MSI-X)
     - Replace polling loops in `nvme_read_blocks` / `nvme_write_blocks`
     - Completion handler via IDT vector
     - Synchronization primitive (semaphore or wait flag) to block caller until I/O done

# Low Priority

## Network stack
- [ ] Study IwIP integration requirements
- [ ] Implement `sys_arch` layer for Novaria (threads/semaphores if needed)
- [ ] Write RTL8139 driver
     - Probe/init
     - Packet reception (to DMA pages!)
     - Packet transmission (from DMA pages!)
- [ ] Integrate with IwIP
     - `ethernetif_init`
     - `ethernetif_input` (packet -> IwIP)
     - `ethernetif_output` (IwIP -> packet)
- [ ] Test ARP, ICMP (ping), UDP

## Extra syscall cleanup
- [ ] Audit other syscalls for garbage
