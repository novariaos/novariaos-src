# Roadmap for NovariaOS 0.2.0

## High Priority
- [X] Refactor internal shell
- [X] VFS Refactoring for Mountable Filesystems
- [ ] Block Device Abstraction Layer
- [ ] EXT2 Filesystem Driver (Initial support)

## Medium Priority
- [ ] /dev/kbd
- [ ] /dev/tty0
- [ ] Poor /sys/pci (vendor/device without interrupts)

## Low Priority 
- [ ] Shell's removal from the kernel
- [ ] Nutils:
    - [ ] cat
    - [ ] ls
- [ ] Passing ARGC, ARGV for binaries running from the Internal Shell
- [ ] Full init system