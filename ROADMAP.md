# Roadmap for NovariaOS 0.2.0

## High Priority
- [x] Refactor internal shell
- [x] VFS Refactoring for Mountable Filesystems
- [x] Block Device Abstraction Layer
- [x] Endianness conversion utilities (le16/le32/le64, be16/be32/be64)
- [ ] EXT2 Filesystem Driver (Initial support)
    - [x] Superblock and Group Descriptor Table parsing
    - [ ] Bitmap management & Resource allocation (#14)
    - [ ] Inode and Directory entry management (#14)
    - [ ] Basic Read/Write operations
- [ ] FAT32 Filesystem Driver (Initial support)

## Medium Priority
- [ ] /dev/console (WIP: current stage — write only)
- [ ] Poor /sys/pci (vendor/device without interrupts)

## Low Priority 
- [ ] Shell's removal from the kernel
- [ ] Nutils:
    - [ ] cat
    - [ ] ls
- [ ] Passing ARGC, ARGV for binaries running from the Internal Shell
- [ ] Full init system