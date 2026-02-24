# Roadmap for NovariaOS 0.2.0

## High Priority
- [x] Refactor internal shell
- [x] VFS Refactoring for Mountable Filesystems
- [x] Block Device Abstraction Layer
- [x] Endianness conversion utilities (le16/le32/le64, be16/be32/be64)
- [x] Support kernel modules
- [ ] Memory manager rework:
    - [X] Buddy
        - [X] Buddy allocation
        - [X] Buddy free
    - [ ] Slab
        - [ ] Slab allocation
        - [ ] Slab free
    - [ ] Kstd allocations (WIP: current stage — buddy only)
        - [ ] kmalloc
        - [ ] kfree
- [ ] EXT2 Filesystem Driver (Initial support)
    - [x] Superblock and Group Descriptor Table parsing
    - [x] Bitmap management & Resource allocation (#14)
    - [x] Inode and Directory entry management (#14)
    - [ ] Basic Read/Write operations
- [ ] FAT32 Filesystem Driver (Initial support)
    - [x] Boot sector and FAT parsing
    - [x] Cluster chain management
    - [x] Directory entry reading
    - [x] Basic Read operations
    - [ ] Basic Write operations

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
