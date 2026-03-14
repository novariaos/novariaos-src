# Roadmap for NovariaOS 0.2.0

## High Priority
- [x] Refactor internal shell
- [x] VFS Refactoring for Mountable Filesystems
- [x] Block Device Abstraction Layer
- [x] Endianness conversion utilities (le16/le32/le64, be16/be32/be64)
- [x] Support kernel modules
- [x] Memory manager rework:
    - [X] Buddy
        - [X] Buddy allocation
        - [X] Buddy free
    - [x] Slab
        - [x] Slab allocation
        - [x] Slab free
    - [x] Kstd allocations (WIP: current stage — buddy only)
        - [x] kmalloc
        - [x] kfree
- [x] EXT2 Filesystem Driver (Initial support)
    - [x] Superblock and Group Descriptor Table parsing
    - [x] Bitmap management & Resource allocation (#14)
    - [x] Inode and Directory entry management (#14)
    - [x] Basic Read/Write operations
- [ ] FAT32 Filesystem Driver (Initial support)
    - [x] Boot sector and FAT parsing
    - [x] Cluster chain management
    - [x] Directory entry reading
    - [x] Basic Read operations
    - [x] Basic Write operations
    - [x] Directory creation (mkdir)
    - [x] Directory removal (rmdir)
    - [x] File deletion (unlink)

- [x] IDE/ATA Disk Driver
    - [x] ATA PIO detection (IDENTIFY)
    - [x] LBA28 read
    - [x] LBA28 write

- [x] NVMe Driver
    - [x] NVMe controller initialization (reset, enable)
    - [x] NVMe admin queue setup and management
    - [x] NVMe I/O queue setup and management
    - [x] MMIO register access functions
    - [x] Identify namespace command
    - [x] NVMe read operations
    - [x] NVMe write operations
    - [x] Interrupt-based I/O (MSI via PCI, vector 0x41)
    - [x] PCI enumeration for NVMe devices
    - [x] Multiple namespace support

## Medium Priority
- [ ] /dev/tty (WIP: current stage — write only)
- [ ] Poor /sys/pci (vendor/device without interrupts)

## Low Priority 
- [ ] Shell's removal from the kernel
- [ ] Nutils:
    - [ ] cat
    - [ ] ls
- [X] Passing ARGC, ARGV for binaries running from the Internal Shell
- [ ] Full init system
