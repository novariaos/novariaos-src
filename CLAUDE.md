# NovariaOS - AI Assistant Context Guide

**Version:** 0.2.0 | **License:** GPL-3.0-only

NovariaOS is an experimental operating system that challenges traditional OS architecture. It uses **NVM (Novaria Virtual Machine)** bytecode as the unified execution environment for all userspace programs, and **CAPS (Capability-Based Security)** for fine-grained permission management without a superuser.

This document serves as a comprehensive guide for AI assistants and contributors working on NovariaOS. It documents architecture, codebase structure, coding conventions, and API interfaces.

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Architecture and Components](#2-architecture-and-components)
   - [2.1 NVM (Novaria Virtual Machine)](#21-nvm-novaria-virtual-machine)
   - [2.2 CAPS (Capability-Based Security)](#22-caps-capability-based-security)
   - [2.3 File Systems](#23-file-systems)
   - [2.4 System Call Interface](#24-system-call-interface)
   - [2.5 Drivers](#25-drivers)
3. [Codebase Structure](#3-codebase-structure)
   - [3.1 Directory Organization](#31-directory-organization)
   - [3.2 Key Entry Points](#32-key-entry-points)
   - [3.3 Build System](#33-build-system)
4. [Conventions and Standards](#4-conventions-and-standards)
   - [4.1 Code Style](#41-code-style)
   - [4.2 Logging and Debugging](#42-logging-and-debugging)
   - [4.3 Error Handling](#43-error-handling)
   - [4.4 Memory Management](#44-memory-management)
5. [API and Interfaces](#5-api-and-interfaces)
   - [5.1 NVM Bytecode API](#51-nvm-bytecode-api)
   - [5.2 Filesystem Operations Interface](#52-filesystem-operations-interface)
   - [5.3 Block Device Interface](#53-block-device-interface)
   - [5.4 Syscall Implementation](#54-syscall-implementation)
6. [Development Workflow](#6-development-workflow)
   - [6.1 Adding a New Syscall](#61-adding-a-new-syscall)
   - [6.2 Implementing a New Filesystem](#62-implementing-a-new-filesystem)
   - [6.3 Adding a Block Device Driver](#63-adding-a-block-device-driver)
   - [6.4 Commit Message Convention](#64-commit-message-convention)
7. [Common Patterns and Idioms](#7-common-patterns-and-idioms)
   - [7.1 String Handling](#71-string-handling)
   - [7.2 Iteration Patterns](#72-iteration-patterns)
   - [7.3 Device Registration Pattern](#73-device-registration-pattern)
8. [Troubleshooting and Debugging](#8-troubleshooting-and-debugging)
   - [8.1 Build Issues](#81-build-issues)
   - [8.2 Runtime Debugging](#82-runtime-debugging)
   - [8.3 NVM Debugging](#83-nvm-debugging)
9. [Current State and Roadmap](#9-current-state-and-roadmap)
   - [9.1 Completed (v0.2.0)](#91-completed-v020)
   - [9.2 In Progress](#92-in-progress)
   - [9.3 Future Vision](#93-future-vision)
10. [Quick Reference](#10-quick-reference)
    - [10.1 Key File Paths](#101-key-file-paths)
    - [10.2 Key Constants](#102-key-constants)
    - [10.3 Quick Commands](#103-quick-commands)

---

## 1. Project Overview

### What is NovariaOS?

NovariaOS is an experimental operating system whose architecture challenges traditional approaches. It is **not** attempting to replace Linux, BSD, Windows, or macOS, but rather aims to establish itself as a respected niche OS with its own unique ecosystem.

### Core Architecture Principles

1. **NVM (Novaria Virtual Machine)** - A unified execution environment where **ALL** programs are compiled to NVM bytecode and executed within the VM, providing:
   - **Portability**: True "Write Once, Run Anywhere" across NovariaOS installations
   - **Memory Safety**: Out-of-bounds conditions won't cause kernel panics
   - **Consistent execution model**: All applications run in the same environment

2. **CAPS (Capability-Based Security)** - A permission system that eliminates the need for a superuser (root), using transferable access rights instead of traditional user/group models:
   - Fine-grained permissions (read, write, create, delete files separately)
   - Transferable capabilities between processes
   - No privileged superuser account

### Philosophy

NovariaOS follows a **pragmatic experimental** approach:
- **NOT POSIX-compliant** - We design our own APIs as we see fit
- **NOT trying to replace mainstream OSes** - We aim for a distinct niche
- **Respect proven concepts** - We don't reinvent everything, only what we're uncertain about
- **Legendary stability goal** - Inspired by FreeBSD's reputation for reliability

### Documentation Structure

- **[docs/1.1-What-is-Novaria.md](docs/1.1-What-is-Novaria.md)** - Project philosophy and core concepts
- **[docs/1.2-Our-goals.md](docs/1.2-Our-goals.md)** - Goals and non-goals
- **[docs/2.* - NVM Documentation](docs/)** - Virtual machine and bytecode specs
- **[docs/3.* - System API](docs/)** - Syscalls and interfaces
- **[docs/4.* - File Systems](docs/)** - VFS and filesystem implementations
- **[ROADMAP.md](ROADMAP.md)** - Current development priorities

---

## 2. Architecture and Components

### 2.1 NVM (Novaria Virtual Machine)

NVM is the heart of NovariaOS userspace. Every program, regardless of the original programming language, is compiled into NVM bytecode and executed in this virtual machine.

#### Process Structure

Defined in [include/core/kernel/nvm/nvm.h](include/core/kernel/nvm/nvm.h):13-32

```c
typedef struct {
    uint8_t* bytecode;          // Bytecode pointer
    int32_t ip;                 // Instruction Pointer
    int32_t stack[STACK_SIZE];  // Data stack (1024 entries)
    int32_t sp;                 // Stack Pointer
    bool active;                // Process is active?
    uint32_t size;              // Bytecode size
    int32_t exit_code;          // Exit code
    int32_t locals[MAX_LOCALS]; // Local variables (512 slots)
    uint16_t capabilities[MAX_CAPS];  // List of caps (max 16)
    uint8_t caps_count;         // Count active caps
    uint8_t pid;                // Process ID
    bool blocked;               // Process blocked waiting for message
    int8_t wakeup_reason;       // Reason for wakeup
} nvm_process_t;
```

#### Key Characteristics

- **Stack-based VM**: All operations work with a 32-bit signed integer stack
- **Stack size**: 4KB (1024 x 32-bit entries)
- **Process limits**: Maximum 32,768 processes (`MAX_PROCESSES`)
- **Local variables**: 512 per process (`MAX_LOCALS`)
- **Bytecode signature**: `NVM0` (0x4E 0x56 0x4D 0x30) required at file start
- **Little-endian**: Multi-byte values stored in little-endian order

#### Bytecode Format

From [docs/2.1-Bytecode.md](docs/2.1-Bytecode.md):

**27 Total Instructions** across 6 categories:

1. **Basic (6)**: HALT, NOP, PUSH, POP, DUP, SWAP
2. **Arithmetic (5)**: ADD, SUB, MUL, DIV, MOD
3. **Comparisons (5)**: CMP, EQ, NEQ, GT, LT
4. **Flow Control (5)**: JMP, JZ, JNZ, CALL, RET
5. **Memory (4)**: LOAD, STORE, LOAD_ABS, STORE_ABS
6. **System (2)**: SYSCALL, BREAK

Example bytecode (from [samples/fssyscalls.asm](samples/fssyscalls.asm)):

```assembly
.NVM0           ; Required signature
push 0          ; Null terminator
push 47         ; '/'
push 116        ; 't'
push 101        ; 'e'
push 115        ; 's'
push 116        ; 't'
syscall open    ; Open /test
store 0         ; Save file descriptor to local var 0
```

#### Scheduler

From [docs/2.3-Sheduling.md](docs/2.3-Sheduling.md):

- **Algorithm**: Time-sliced Round Robin
- **Time slice**: 2ms (`TIME_SLICE_MS = 2`)
- **Instructions per tick**: Up to 100 instructions executed per scheduler tick
- **Execution model**: Quasi-preemptive (instruction-level granularity)
- **Scheduler function**: `nvm_scheduler_tick()` in [core/kernel/nvm/nvm.c](core/kernel/nvm/nvm.c)

#### Process States

- **Active** (`active = true`) - Process is running or ready to run
- **Blocked** (`blocked = true`) - Process waiting for message (IPC)
- **Inactive** (`active = false`) - Process has exited

---

### 2.2 CAPS (Capability-Based Security)

CAPS is NovariaOS's permission system that replaces traditional user/group/root models with fine-grained, transferable capabilities.

#### Capability System Overview

From [include/core/kernel/nvm/caps.h](include/core/kernel/nvm/caps.h):

- **Capability type**: `uint16_t` (16-bit unsigned integer)
- **Max capabilities per process**: 16 (`MAX_CAPS`)
- **Transferable**: Capabilities can be passed between processes during spawn
- **No superuser**: All permissions are capability-based, no root account

#### Complete Capability List

From [docs/2.2-Caps.md](docs/2.2-Caps.md) and [include/core/kernel/nvm/caps.h](include/core/kernel/nvm/caps.h):

| Capability | Value | Description |
|------------|-------|-------------|
| `CAPS_NONE` | 0x0000 | No capabilities |
| `CAP_FS_READ` | 0x0001 | Read files |
| `CAP_FS_WRITE` | 0x0002 | Write to files |
| `CAP_FS_CREATE` | 0x0003 | Create new files |
| `CAP_FS_DELETE` | 0x0004 | Delete files |
| `CAP_MEM_MGMT` | 0x0005 | Memory management |
| `CAP_DRV_ACCESS` | 0x0006 | Direct driver/hardware access |
| `CAP_PROC_MGMT` | 0x0007 | Process management |
| `CAP_CAPS_MGMT` | 0x0008 | Capability management |
| `CAP_DRV_GROUP_STORAGE` | 0x0100 | Storage device group |
| `CAP_DRV_GROUP_VIDEO` | 0x0200 | Video device group |
| `CAP_DRV_GROUP_AUDIO` | 0x0300 | Audio device group |
| `CAP_DRV_GROUP_NETWORK` | 0x0400 | Network device group |
| `CAP_ALL` | 0xFFFF | All capabilities (dev/debug only) |

#### Capability Management API

From [include/core/kernel/nvm/caps.h](include/core/kernel/nvm/caps.h):

```c
bool caps_has_capability(nvm_process_t* proc, int16_t cap);
bool caps_add_capability(nvm_process_t* proc, int16_t cap);
bool caps_remove_capability(nvm_process_t* proc, int16_t cap);
void caps_clear_all(nvm_process_t* proc);
bool caps_copy(nvm_process_t* dest, nvm_process_t* src);
```

**Usage Pattern**:

```c
// Check if process has capability before performing operation
if (!caps_has_capability(proc, CAP_FS_READ)) {
    return -EPERM;  // Permission denied
}
```

#### Capability Inheritance

When spawning a new process via `SYS_SPAWN`, capabilities can be passed to the child process. The spawning process must have `CAP_CAPS_MGMT` to manage capabilities during spawn.

---

### 2.3 File Systems

NovariaOS uses a Virtual File System (VFS) abstraction layer with support for mount points and multiple filesystem types. **Major refactoring completed in v0.2.0** introduced the mount point architecture.

#### VFS Architecture

From [include/core/fs/vfs.h](include/core/fs/vfs.h) and [docs/4.1-VFS.md](docs/4.1-VFS.md):

**File Types**:
- `VFS_TYPE_FILE` - Regular files
- `VFS_TYPE_DIR` - Directories
- `VFS_TYPE_DEVICE` - Device files (/dev/null, /dev/zero, etc.)

**File Descriptors**:
- `0, 1, 2` - stdin, stdout, stderr (standard streams)
- `3+` - User-opened files
- Special reserved FDs: 1000-1005 for /dev/null, /dev/zero, /dev/full, etc.

**Limits** (from [include/core/fs/vfs.h](include/core/fs/vfs.h)):
```c
#define MAX_FILES 256        // Maximum total files
#define MAX_HANDLES 64       // Maximum open file handles
#define MAX_FILENAME 256     // Maximum filename length (bytes)
#define MAX_FILE_SIZE 65536  // Maximum file size (64KB)
#define MAX_MOUNTS 32        // Maximum mount points
```

**Error Codes** (POSIX-like):
```c
#define ENOENT  2   // No such file or directory
#define EPERM   1   // Operation not permitted
#define EACCES  13  // Permission denied
#define ENOMEM  12  // Out of memory
#define ENOSPC  28  // No space left on device
#define EBADF   9   // Bad file descriptor
#define ENOTDIR 20  // Not a directory
#define EROFS   30  // Read-only file system
#define EINVAL  22  // Invalid argument
#define EEXIST  17  // File exists
```

#### Filesystem Operations Interface

**THE KEY ABSTRACTION**: From [include/core/fs/vfs.h](include/core/fs/vfs.h):127-154

```c
struct vfs_fs_ops {
    const char* name;  // Filesystem type name (e.g., "ext2", "iso9660")

    // Lifecycle operations
    int (*mount)(vfs_mount_t* mnt, const char* device, void* data);
    int (*unmount)(vfs_mount_t* mnt);

    // File operations
    int (*open)(vfs_mount_t* mnt, const char* path, int flags, vfs_file_handle_t* h);
    int (*close)(vfs_mount_t* mnt, vfs_file_handle_t* h);
    vfs_ssize_t (*read)(vfs_mount_t* mnt, vfs_file_handle_t* h, void* buf, size_t count);
    vfs_ssize_t (*write)(vfs_mount_t* mnt, vfs_file_handle_t* h, const void* buf, size_t count);
    vfs_off_t (*seek)(vfs_mount_t* mnt, vfs_file_handle_t* h, vfs_off_t offset, int whence);

    // Directory operations
    int (*mkdir)(vfs_mount_t* mnt, const char* path, uint32_t mode);
    int (*rmdir)(vfs_mount_t* mnt, const char* path);
    int (*readdir)(vfs_mount_t* mnt, const char* path, vfs_dirent_t* entries, size_t max_entries);

    // Metadata operations
    int (*stat)(vfs_mount_t* mnt, const char* path, vfs_stat_t* stat);
    int (*unlink)(vfs_mount_t* mnt, const char* path);

    // Optional operations
    int (*ioctl)(vfs_mount_t* mnt, vfs_file_handle_t* h, unsigned long req, void* arg);
    int (*sync)(vfs_mount_t* mnt);
};
```

#### Mount Points

From [include/core/fs/vfs.h](include/core/fs/vfs.h):164-173

```c
struct vfs_mount {
    char mount_point[MAX_MOUNT_PATH];  // e.g., "/", "/mnt/cdrom"
    char device[MAX_MOUNT_PATH];       // e.g., "/dev/cdrom0"
    vfs_filesystem_t* fs;              // Pointer to filesystem driver
    void* fs_private;                  // Private data for FS instance
    uint32_t flags;                    // Mount flags (VFS_MNT_READONLY, etc.)
    bool mounted;                      // Is this mount active?
    int ref_count;                     // Reference count for open files
};
```

**Mounting a Filesystem**:

```c
// Register filesystem driver first
vfs_register_filesystem("myfs", &myfs_ops, 0);

// Then mount an instance
vfs_mount_fs("myfs", "/mnt/point", "/dev/device", 0, NULL);
```

#### Block Device Abstraction Layer (NEW in v0.2.0)

From [include/core/fs/block.h](include/core/fs/block.h):

**Purpose**: Provide abstraction for block-based storage devices (HDDs, SSDs, CD-ROMs).

```c
typedef struct {
    int (*read_blocks)(struct block_device* dev, uint64_t lba, size_t count, void* buf);
    int (*write_blocks)(struct block_device* dev, uint64_t lba, size_t count, const void* buf);
} block_device_ops_t;

typedef struct block_device {
    char name[32];                // Device name (e.g., "cdrom0")
    uint32_t block_size;          // Size of a single block in bytes
    uint64_t total_blocks;        // Total number of blocks
    block_device_ops_t ops;       // Driver operations
    void* private_data;           // For driver's internal use
    bool used;
} block_device_t;
```

**Registration API**:

```c
int register_block_device(const char* name, uint32_t block_size,
                         uint64_t total_blocks, block_device_ops_t* ops,
                         void* private_data);
block_device_t* find_block_device(const char* name);
```

Block devices automatically appear in `/dev/` via the `block_dev_vfs` integration layer.

#### Current Filesystems

1. **ramfs** - Simple in-memory filesystem (32 sectors, 4KB each)
   - Implementation: [core/fs/ramfs.c](core/fs/ramfs.c)
   - Docs: [docs/4.3-ramfs.md](docs/4.3-ramfs.md)

2. **iso9660** - Read-only CD-ROM filesystem
   - Implementation: [core/fs/iso9660.c](core/fs/iso9660.c)
   - Docs: [docs/4.2-iso9660.md](docs/4.2-iso9660.md)
   - Mounted automatically from Limine boot module

3. **devfs** - Device pseudo-filesystem (`/dev/`)
   - Implementation: [core/fs/devfs.c](core/fs/devfs.c)
   - Provides: /dev/null, /dev/zero, /dev/full, /dev/stdin, /dev/stdout, /dev/stderr

4. **procfs** - Process information filesystem (`/proc/`)
   - Implementation: [core/fs/procfs.c](core/fs/procfs.c)
   - Provides: /proc/meminfo, /proc/cpuinfo, /proc/<pid>/

5. **ext2** - Planned for v0.2.0 (IN PROGRESS)
   - Not yet implemented
   - See [ROADMAP.md](ROADMAP.md)

---

### 2.4 System Call Interface

Syscalls are the primary interface between userspace NVM programs and the kernel. All system calls are invoked via the `SYSCALL` bytecode instruction.

#### Calling Convention

From [docs/2.1-Bytecode.md](docs/2.1-Bytecode.md):

- **Instruction**: `SYSCALL xx` where `xx` is the syscall number
- **Arguments**: Passed **right-to-left** on the stack
- **Return value**: Pushed to the stack after syscall completes
- **Capability checks**: Enforced before syscall execution

**Example** (from [samples/fssyscalls.asm](samples/fssyscalls.asm)):

```assembly
; Open a file: int fd = open("/test.txt", flags)
push 0      ; Null terminator
push 116    ; 't'
push 120    ; 'x'
; ... more characters ...
push 47     ; '/'
syscall open  ; Call SYS_OPEN (0x02)
store 0       ; Save FD to local variable 0
```

#### Complete Syscall Table

From [docs/3.1-Syscalls.md](docs/3.1-Syscalls.md):

| Name | Number | Description | Required Capability |
|------|--------|-------------|---------------------|
| `EXIT` | 0x00 | Exit process with code | - |
| `SPAWN` | 0x01 | Spawn new program, inherit caps | `CAP_FS_READ` |
| `OPEN` | 0x02 | Open file, return FD | `CAP_FS_READ` |
| `READ` | 0x03 | Read from file descriptor | `CAP_FS_READ` |
| `WRITE` | 0x04 | Write to file descriptor | `CAP_FS_WRITE` |
| `CREATE` | 0x05 | Create new file | `CAP_FS_CREATE` |
| `DELETE` | 0x06 | Delete file | `CAP_FS_DELETE` |
| `CAP_CHECK` | 0x07 | Check capability via PID | - |
| `CAP_SPAWN` | 0x08 | Grant capabilities | `CAP_CAPS_MGMT` |
| `MSG_SEND` | 0x0A | Send message (IPC) | - |
| `MSG_RECV` | 0x0B | Receive message (IPC) | - |
| `PORT_IN_BYTE` | 0x0C | Read byte from I/O port | `CAP_DRV_ACCESS` |
| `PORT_OUT_BYTE` | 0x0D | Write byte to I/O port | `CAP_DRV_ACCESS` |

#### Syscall Handler

Implementation in [core/kernel/nvm/syscalls.c](core/kernel/nvm/syscalls.c):29-150

**Pattern**:

```c
int32_t syscall_handler(uint8_t syscall_id, nvm_process_t* proc) {
    int32_t result = 0;

    switch(syscall_id) {
        case SYS_OPEN: {
            // 1. Check capability
            if (!caps_has_capability(proc, CAP_FS_READ)) {
                result = -EPERM;
                break;
            }

            // 2. Pop arguments from stack (right-to-left)
            if (proc->sp < 1) {
                LOG_WARN("Stack underflow\n");
                result = -1;
                break;
            }

            // 3. Extract string from stack (null-terminated)
            // 4. Perform operation
            int fd = vfs_open(filename, flags);

            // 5. Return result (will be pushed to stack)
            result = fd;
            break;
        }
    }

    return result;
}
```

---

### 2.5 Drivers

NovariaOS currently has minimal driver support, focusing on essential hardware for development.

#### Current Drivers

1. **Serial (COM1)** - [core/drivers/serial.c](core/drivers/serial.c)
   - Purpose: Debug logging output
   - Output to: `qemu.log` (via `-serial file:qemu.log`)
   - Functions: `init_serial()`, `write_serial()`, `serial_print()`

2. **Timer (PIT)** - [core/drivers/timer.c](core/drivers/timer.c)
   - Frequency: 1000 Hz (1ms ticks)
   - Purpose: Scheduler timing, `timer_ticks` global counter
   - Functions: `pit_init()`, `pit_polling_loop()`

3. **Keyboard (PS/2)** - [core/drivers/keyboard.c](core/drivers/keyboard.c)
   - Buffer: 256-byte circular buffer
   - Scancode mapping to ASCII
   - Functions: `keyboard_init()`, `keyboard_handler()`, `keyboard_getchar()`

4. **CD-ROM** - [core/drivers/cdrom.c](core/drivers/cdrom.c)
   - Block device interface
   - Registered via block layer
   - Used for iso9660 filesystem

#### Driver Development Pattern

From code analysis:

1. Define hardware constants (port addresses, registers)
2. Implement initialization function (`driver_init()`)
3. Implement device operations structure
4. Register with VFS or block layer
5. Provide device-specific API

**Example** (simplified serial driver):

```c
#define PORT 0x3F8  // COM1

int init_serial() {
    outb(PORT + 1, 0x00);    // Disable interrupts
    outb(PORT + 3, 0x80);    // Enable DLAB
    outb(PORT + 0, 0x03);    // Set divisor
    outb(PORT + 1, 0x00);
    outb(PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
    return 0;
}

void write_serial(char a) {
    while (is_transmit_empty() == 0);
    outb(PORT, a);
}
```

---

## 3. Codebase Structure

### 3.1 Directory Organization

```
/home/ildar/Документы/code/novariaos-src-3/
├── core/                           # Kernel and core components
│   ├── arch/                      # Architecture-specific code (x86_64)
│   │   ├── boot.asm              # Bootstrap assembly (entry point)
│   │   ├── cpuid.c               # CPU identification
│   │   ├── entropy.c             # Hardware RNG
│   │   ├── idt.c                 # Interrupt Descriptor Table
│   │   └── io.c                  # I/O port operations
│   ├── crypto/                   # Cryptography implementations
│   │   ├── chacha20.c           # ChaCha20 cipher
│   │   └── chacha20_rng.c       # ChaCha20-based RNG
│   ├── drivers/                  # Hardware drivers
│   │   ├── serial.c             # COM port serial driver
│   │   ├── timer.c              # PIT timer (1000 Hz)
│   │   ├── keyboard.c           # PS/2 keyboard
│   │   └── cdrom.c              # CD-ROM driver
│   ├── fs/                       # Filesystem implementations
│   │   ├── vfs.c                # Virtual File System (CRITICAL: 28KB)
│   │   ├── block.c              # Block device abstraction
│   │   ├── block_dev_vfs.c      # Block device /dev/ integration
│   │   ├── iso9660.c            # ISO 9660 filesystem
│   │   ├── ramfs.c              # RAM filesystem
│   │   ├── initramfs.c          # Initial RAM filesystem
│   │   ├── devfs.c              # Device filesystem (/dev/)
│   │   └── procfs.c             # Process filesystem (/proc/)
│   └── kernel/                   # Core kernel
│       ├── kernel.c             # Main entry point (kmain)
│       ├── mem.c                # Memory management
│       ├── kstd.c               # Kernel standard library
│       ├── shell.c              # Internal shell (WIP: removal planned)
│       ├── userspace.c          # Userspace initialization
│       ├── nvm/                 # NVM subsystem
│       │   ├── nvm.c           # VM execution engine (29KB, CRITICAL)
│       │   ├── syscalls.c      # Syscall handlers (15KB)
│       │   └── caps.c          # Capability management
│       └── vge/                 # Video Graphics Engine
│           ├── fb.c            # Framebuffer management
│           ├── fb_render.c     # Rendering functions
│           └── psf.c           # PSF font handling
│
├── include/                      # All header files
│   └── core/                    # Mirrors core/ structure
│       ├── kernel/nvm/nvm.h           # NVM process structure (CRITICAL)
│       ├── kernel/nvm/caps.h          # Capability definitions
│       ├── kernel/nvm/syscall.h       # Syscall numbers
│       ├── fs/vfs.h                   # VFS interface (CRITICAL: 228 lines)
│       ├── fs/block.h                 # Block device interface
│       ├── kernel/log.h               # Logging macros
│       └── [other headers mirror core/]
│
├── rootfs/                       # Root filesystem contents
│   ├── etc/                     # Configuration files
│   └── usr/src/                 # Userspace programs (compiled into kernel)
│       ├── echo.c              # Echo utility
│       ├── clear.c             # Clear screen
│       ├── rm.c                # Remove file
│       ├── write.c             # Write to file
│       ├── nova.c              # Nova shell (9KB)
│       ├── uname.c             # System information
│       └── userspace_init.c    # Userspace initialization
│
├── samples/                      # Example NVM assembly programs
│   ├── fssyscalls.asm          # Filesystem syscall demo
│   ├── test_cdrom.asm          # CD-ROM test
│   └── random.asm              # Random number test
│
├── tools/                        # Build tools
│   └── nvma/                   # NVM assembler
│       └── src/main.c          # Assembler implementation
│
├── docs/                         # Technical documentation
│   ├── 1.* - Introduction (Overview, What is NovariaOS, Goals, Build)
│   ├── 2.* - NVM (What is NVM, Bytecode, Caps, Scheduling)
│   ├── 3.* - System API (SysAPI, Syscalls, Individual syscall docs)
│   └── 4.* - File Systems (Overview, VFS, iso9660, ramfs, devfs, procfs)
│
├── lib/                          # Libraries
│   ├── bootloader/             # Limine bootloader integration
│   └── nc/                     # Novaria C library
│
├── limine/                       # Limine bootloader files
├── chorus.build                  # Build configuration (Chorus build system)
├── link.ld                       # Linker script
├── ROADMAP.md                    # Development roadmap
├── README.md                     # Project README
└── CLAUDE.md                     # This file
```

### 3.2 Key Entry Points

#### Kernel Boot Sequence

1. **[core/arch/boot.asm](core/arch/boot.asm)** - Assembly bootstrap
   - Sets up stack, GDT, IDT
   - Calls `kmain()` from C code

2. **[core/kernel/kernel.c](core/kernel/kernel.c):kmain()** (line ~64) - Main kernel entry
   - Initializes memory manager
   - Initializes VFS, block layer, filesystems
   - Detects and mounts ISO9660 from Limine module
   - Loads initramfs programs into VFS
   - Initializes NVM
   - Starts internal shell (temporary)

**Initialization Order** (from kernel.c):

```c
void kmain() {
    initializeMemoryManager();  // Memory first
    init_serial();              // Serial for logging
    syslog_init();              // System log
    ramfs_init();               // RamFS
    vfs_init();                 // VFS layer
    block_init();               // Block devices
    keyboard_init();            // Keyboard

    // Mount filesystems
    iso9660_mount_from_module();  // Mount CD-ROM
    load_initramfs();             // Load initramfs

    nvm_init();                   // NVM VM
    block_dev_vfs_init();         // Block device /dev/ integration

    shell_init();                 // Internal shell
    shell_run();                  // Enter shell loop
}
```

#### NVM Execution

- **[core/kernel/nvm/nvm.c](core/kernel/nvm/nvm.c):nvm_init()** - Initialize VM
- **[core/kernel/nvm/nvm.c](core/kernel/nvm/nvm.c):nvm_scheduler_tick()** - Scheduler loop
- **[core/kernel/nvm/nvm.c](core/kernel/nvm/nvm.c):nvm_create_process()** - Create new process

#### Filesystem Operations

- **[core/fs/vfs.c](core/fs/vfs.c):vfs_init()** - VFS initialization
- **[core/fs/vfs.c](core/fs/vfs.c):vfs_mount_fs()** - Mount filesystem
- **[core/fs/vfs.c](core/fs/vfs.c):vfs_open()** - Open file
- **[core/fs/vfs.c](core/fs/vfs.c):vfs_find_mount()** - Find mount point for path

---

### 3.3 Build System

NovariaOS uses **Chorus** build system ([chorus.build](chorus.build)).

#### Key Build Targets

From [chorus.build](chorus.build):

```bash
# Default: Build ISO image
chorus all

# Build kernel binary only
chorus kernel.bin

# Build NVM assembler
chorus nvma

# Build and run in QEMU
chorus all run

# Production release build
chorus build-release
```

#### Build Flow

1. **Prepare**: Create build directories
   ```bash
   mkdir -p dist
   mkdir -p build/objects
   ```

2. **Compile**: All `.c` files to `.o` objects
   - Flags: `-I./include/ -I./ -fno-stack-protector -m64 -mcmodel=large -nostdlib -mno-red-zone -mno-mmx -mno-sse -mno-sse2`
   - Architecture: x86_64

3. **Assemble**: `core/arch/boot.asm`
   - Assembler: nasm
   - Format: ELF64

4. **Link**: All objects with [link.ld](link.ld) linker script
   - Output: `build/boot/kernel.bin`
   - Flags: `-nostdlib -m elf_x86_64 -T link.ld -z noexecstack`

5. **Create initramfs**: Package `rootfs/` contents
   ```bash
   ruby initramfs-rebuild.rb
   xorriso -as mkisofs -o build/boot/rootfs.img rootfs/
   ```

6. **Create ISO**: Bootable ISO with Limine bootloader
   ```bash
   xorriso -as mkisofs -b limine-bios-cd.bin -o dist/build_YYYY-MM-DD.iso build/boot/
   limine bios-install dist/build_YYYY-MM-DD.iso
   ```

7. **Run**: QEMU with 1GB RAM, serial logging
   ```bash
   qemu-system-x86_64 -m 1024M -serial file:qemu.log -cdrom dist/build_YYYY-MM-DD.iso
   ```

#### Key Compiler Flags

- `-fno-stack-protector` - Disable stack canaries (kernel-level)
- `-m64` - 64-bit mode
- `-mcmodel=large` - Large memory model
- `-nostdlib` - No standard library (freestanding environment)
- `-mno-red-zone` - Disable red zone (kernel requirement)
- `-mno-mmx -mno-sse -mno-sse2` - Disable SIMD instructions

---

## 4. Conventions and Standards

### 4.1 Code Style

#### Naming Conventions

From codebase analysis:

- **Functions**: `snake_case` with subsystem prefix
  - Examples: `vfs_open()`, `nvm_create_process()`, `ramfs_write()`

- **Types**: `snake_case_t` suffix for typedefs
  - Examples: `nvm_process_t`, `vfs_mount_t`, `block_device_t`

- **Macros/Constants**: `SCREAMING_SNAKE_CASE`
  - Examples: `MAX_PROCESSES`, `CAP_FS_READ`, `STACK_SIZE`

- **Global variables**: `snake_case`
  - Examples: `current_process`, `timer_ticks`

- **Struct members**: `snake_case`
  - Examples: `bytecode`, `stack_pointer`, `mount_point`

#### File Organization

**Standard structure**:

```c
// SPDX-License-Identifier: GPL-3.0-only

// Includes (local headers first, then system)
#include <core/kernel/kstd.h>
#include <core/kernel/mem.h>
#include <stdint.h>
#include <stdbool.h>

// Static data/state
static my_type_t registry[MAX_ITEMS];

// Static helper functions
static int helper_function(void) {
    // ...
}

// Public API functions
void subsystem_init(void) {
    // ...
}
```

#### Header Guards

```c
#ifndef FILENAME_H
#define FILENAME_H

// Content

#endif
```

For subdirectory headers, underscore prefix observed:
```c
#ifndef _NVM_H
#define _NVM_H
```

#### Indentation and Bracing

- **Style**: K&R (opening brace on same line)
- **Indentation**: 4 spaces (NO tabs)

```c
int vfs_open(const char* filename, int flags) {
    if (condition) {
        do_something();
    }
    return 0;
}
```

#### Comments

- **Minimal comments**: Code should be self-explanatory
- **File headers**: SPDX license identifier only
- **Inline comments**: Only for non-obvious logic

```c
// Check for shift press
if (scancode == 0x2A || scancode == 0x36) {
    shift_pressed = true;
}
```

---

### 4.2 Logging and Debugging

#### Log Levels

From [include/core/kernel/log.h](include/core/kernel/log.h):180-186

```c
LOG_FATAL(fmt, ...)    // Critical errors
LOG_ERROR(fmt, ...)    // Errors
LOG_WARN(fmt, ...)     // Warnings
LOG_INFO(fmt, ...)     // Informational messages
LOG_DEBUG(fmt, ...)    // Debug information
LOG_TRACE(fmt, ...)    // Detailed trace information
```

**Current log level**: `LOG_LEVEL_TRACE` (all levels enabled)

#### Format Specifiers

- `%d` - signed int
- `%u` - unsigned int
- `%x` - lowercase hex
- `%X` - uppercase hex
- `%s` - string
- `%c` - char
- `%p` - pointer (0x-prefixed hex)

#### Usage Pattern

```c
LOG_INFO("Registered block device '%s'.", name);
LOG_DEBUG("Process %d: Spawn called with fd=%d, argc=%d\n",
          proc->pid, target_fd, argc);
LOG_WARN("Process %d: Stack underflow\n", proc->pid);
```

#### Debug Output

- **Serial output**: Logged to `qemu.log` file (via `-serial file:qemu.log`)
- **System log**: Available at `/var/log/system.log` pseudo-file

---

### 4.3 Error Handling

#### Return Value Convention

- **Success**: `0` or **positive** values (e.g., file descriptors, PIDs)
- **Failure**: **Negative** errno values (e.g., `-ENOMEM`, `-ENOENT`)

**Example**:

```c
int fd = vfs_open(filename, flags);
if (fd < 0) {
    LOG_ERROR("Failed to open file: %d\n", fd);
    return fd;  // Propagate error
}
```

#### Error Codes

From [include/core/fs/vfs.h](include/core/fs/vfs.h):34-48

```c
#define ENOSPC  28  // No space left on device
#define EACCES  13  // Permission denied
#define EBADF   9   // Bad file descriptor
#define ENOENT  2   // No such file or directory
#define ENOTDIR 20  // Not a directory
#define EROFS   30  // Read-only file system
#define ENOMEM  12  // Out of memory
#define EINVAL  22  // Invalid argument
#define EEXIST  17  // File exists
#define EPERM   1   // Operation not permitted
#define ENODEV  19  // No such device
```

#### Null Checking Pattern

```c
// Check pointers before use
if (!filename || !ops) {
    return -EINVAL;
}

// Check allocation success
char* buffer = kmalloc(size);
if (!buffer) {
    LOG_WARN("Failed to allocate memory\n");
    return -ENOMEM;
}
```

#### Capability Error Pattern

```c
if (!caps_has_capability(proc, CAP_FS_READ)) {
    result = -EPERM;  // Permission denied
    break;
}
```

---

### 4.4 Memory Management

From [include/core/kernel/mem.h](include/core/kernel/mem.h):

#### Allocation API

```c
void* kmalloc(size_t size);  // Alias for allocateMemory
void kfree(void* ptr);        // Alias for freeMemory
```

#### Pattern

```c
void* ptr = kmalloc(size);
if (!ptr) {
    return -ENOMEM;
}

// Use ptr

kfree(ptr);  // NOTE: Some code paths may lack kfree calls
```

#### Safe String Functions

From [include/core/kernel/kstd.h](include/core/kernel/kstd.h):

```c
void strcpy_safe(char* dest, const char* src, size_t max_len);
void strcat_safe(char* dest, const char* src, size_t max_len);
```

**Usage**:

```c
strcpy_safe(dest, src, sizeof(dest));  // Prevents buffer overflow
```

---

## 5. API and Interfaces

### 5.1 NVM Bytecode API

#### Complete Instruction Set

From [docs/2.1-Bytecode.md](docs/2.1-Bytecode.md):

**Basic Instructions (6)**:
- `HALT (0x00)` - Halt the process
- `NOP (0x01)` - No operation
- `PUSH (0x02) <low> <mid1> <mid2> <high>` - Push 32-bit value (little-endian)
- `POP (0x04)` - Remove top of stack
- `DUP (0x05)` - Duplicate top of stack
- `SWAP (0x06)` - Swap top 2 values

**Arithmetic Instructions (5)**:
- `ADD (0x10)` - Add top 2 values
- `SUB (0x11)` - Subtract top 2 values
- `MUL (0x12)` - Multiply top 2 values
- `DIV (0x13)` - Divide top 2 values
- `MOD (0x14)` - Modulo (remainder)

**Comparison Instructions (5)**:
- `CMP (0x20)` - Compare (a-b), push -1/0/1
- `EQ (0x21)` - Equal? Push 0/1
- `NEQ (0x22)` - Not equal? Push 0/1
- `GT (0x23)` - Greater than? Push 0/1
- `LT (0x24)` - Less than? Push 0/1

**Flow Control Instructions (5)** (4-byte addresses):
- `JMP (0x30) <addr>` - Unconditional jump
- `JZ (0x31) <addr>` - Jump if zero (pops value)
- `JNZ (0x32) <addr>` - Jump if not zero (pops value)
- `CALL (0x33) <addr>` - Function call
- `RET (0x34)` - Return from function

**Memory Instructions (4)**:
- `LOAD (0x40) <xx>` - Load from local variable xx
- `STORE (0x41) <xx>` - Store to local variable xx
- `LOAD_ABS (0x44)` - Load from address (address on stack, requires `CAP_DRV_ACCESS`)
- `STORE_ABS (0x45)` - Store to address (requires `CAP_DRV_ACCESS`)

**System Instructions (2)**:
- `SYSCALL (0x50) <xx>` - Invoke syscall number xx
- `BREAK (0x51)` - Debugging breakpoint

#### NVM Assembly Example

From [samples/fssyscalls.asm](samples/fssyscalls.asm):

```assembly
.NVM0           ; Required signature

; Open /test.txt
push 0          ; Null terminator
push 47         ; '/'
push 116        ; 't'
push 101        ; 'e'
push 115        ; 's'
push 116        ; 't'
push 46         ; '.'
push 116        ; 't'
push 120        ; 'x'
push 116        ; 't'
syscall open    ; Call SYS_OPEN (0x02)
store 0         ; Save FD to local variable 0

; Read from file
loop:
    load 0         ; Load FD
    syscall read   ; Read one byte
    dup            ; Duplicate result
    push 0
    gt             ; Check if > 0
    jz end         ; If zero, exit loop

    ; Process byte...

    jmp loop

end:
    push 0
    syscall exit   ; Exit with code 0
```

#### String Handling on Stack

Strings are pushed character-by-character onto the stack, **ending with null terminator (0)**:

```assembly
push 0      ; Null terminator FIRST
push 111    ; 'o'
push 108    ; 'l'
push 108    ; 'l'
push 101    ; 'e'
push 104    ; 'h'
; Stack now contains "hello\0" (null at bottom)
```

---

### 5.2 Filesystem Operations Interface

#### The vfs_fs_ops_t Structure

From [include/core/fs/vfs.h](include/core/fs/vfs.h):127-154

This is **THE KEY ABSTRACTION** for implementing filesystems in NovariaOS.

```c
struct vfs_fs_ops {
    const char* name;  // Filesystem type name (e.g., "ext2")

    // Lifecycle operations
    int (*mount)(vfs_mount_t* mnt, const char* device, void* data);
    int (*unmount)(vfs_mount_t* mnt);

    // File operations
    int (*open)(vfs_mount_t* mnt, const char* path, int flags, vfs_file_handle_t* h);
    int (*close)(vfs_mount_t* mnt, vfs_file_handle_t* h);
    vfs_ssize_t (*read)(vfs_mount_t* mnt, vfs_file_handle_t* h, void* buf, size_t count);
    vfs_ssize_t (*write)(vfs_mount_t* mnt, vfs_file_handle_t* h, const void* buf, size_t count);
    vfs_off_t (*seek)(vfs_mount_t* mnt, vfs_file_handle_t* h, vfs_off_t offset, int whence);

    // Directory operations
    int (*mkdir)(vfs_mount_t* mnt, const char* path, uint32_t mode);
    int (*rmdir)(vfs_mount_t* mnt, const char* path);
    int (*readdir)(vfs_mount_t* mnt, const char* path, vfs_dirent_t* entries, size_t max_entries);

    // Metadata operations
    int (*stat)(vfs_mount_t* mnt, const char* path, vfs_stat_t* stat);
    int (*unlink)(vfs_mount_t* mnt, const char* path);

    // Optional operations (can be NULL)
    int (*ioctl)(vfs_mount_t* mnt, vfs_file_handle_t* h, unsigned long req, void* arg);
    int (*sync)(vfs_mount_t* mnt);
};
```

#### Implementation Pattern

**Step-by-step guide**:

1. **Define operations structure**:

```c
static const vfs_fs_ops_t myfs_ops = {
    .name = "myfs",
    .mount = myfs_mount,
    .unmount = myfs_unmount,
    .open = myfs_open,
    .close = myfs_close,
    .read = myfs_read,
    .write = myfs_write,
    .seek = myfs_seek,
    .mkdir = myfs_mkdir,
    .rmdir = myfs_rmdir,
    .readdir = myfs_readdir,
    .stat = myfs_stat,
    .unlink = myfs_unlink,
    .ioctl = NULL,   // Optional
    .sync = NULL     // Optional
};
```

2. **Register filesystem**:

```c
void myfs_init(void) {
    vfs_register_filesystem("myfs", &myfs_ops, 0);
    LOG_INFO("MyFS registered.");
}
```

3. **Implement mount operation**:

```c
int myfs_mount(vfs_mount_t* mnt, const char* device, void* data) {
    // 1. Open block device if needed
    block_device_t* bdev = find_block_device(device);
    if (!bdev) {
        return -ENODEV;
    }

    // 2. Read superblock/metadata
    // 3. Allocate private data
    myfs_private_t* priv = kmalloc(sizeof(myfs_private_t));
    if (!priv) {
        return -ENOMEM;
    }

    // 4. Initialize private data
    priv->block_device = bdev;
    // ...

    // 5. Store in mount structure
    mnt->fs_private = priv;

    LOG_INFO("MyFS mounted on %s from %s", mnt->mount_point, device);
    return 0;
}
```

4. **Implement file operations** (example: open):

```c
int myfs_open(vfs_mount_t* mnt, const char* path, int flags, vfs_file_handle_t* h) {
    myfs_private_t* priv = (myfs_private_t*)mnt->fs_private;

    // 1. Resolve path to inode/file
    // 2. Allocate private handle data
    myfs_file_t* file_priv = kmalloc(sizeof(myfs_file_t));
    if (!file_priv) {
        return -ENOMEM;
    }

    // 3. Initialize handle
    h->private_data = file_priv;

    return 0;
}
```

#### Key Data Structures

**vfs_mount_t** (from vfs.h:164-173):

```c
struct vfs_mount {
    char mount_point[MAX_MOUNT_PATH];  // e.g., "/", "/mnt/cdrom"
    char device[MAX_MOUNT_PATH];       // e.g., "/dev/cdrom0"
    vfs_filesystem_t* fs;              // Pointer to registered FS driver
    void* fs_private;                  // For FS driver's private data
    uint32_t flags;                    // VFS_MNT_READONLY, etc.
    bool mounted;
    int ref_count;
};
```

**vfs_file_handle_t** (from vfs.h:176-184):

```c
struct vfs_file_handle {
    bool used;
    int fd;                            // File descriptor number
    vfs_mount_t* mount;                // Which mount this file is on
    char path[MAX_FILENAME];           // Path relative to mount point
    vfs_off_t position;                // Current file position
    int flags;                         // VFS_READ, VFS_WRITE, etc.
    void* private_data;                // For FS driver's use
};
```

**vfs_stat_t** (from vfs.h:83-88):

```c
typedef struct vfs_stat {
    uint32_t    st_mode;      // File type and permissions
    vfs_off_t   st_size;      // Size in bytes
    uint32_t    st_blksize;   // Block size for I/O
    uint64_t    st_mtime;     // Modification time
} vfs_stat_t;
```

#### Mount API

```c
// Register filesystem driver (do this in init function)
int vfs_register_filesystem(const char* name, const vfs_fs_ops_t* ops, uint32_t flags);

// Mount an instance
int vfs_mount_fs(const char* fs_name, const char* mount_point,
                 const char* device, uint32_t flags, void* data);

// Unmount
int vfs_umount(const char* mount_point);

// Find mount for a path
vfs_mount_t* vfs_find_mount(const char* path, const char** relative_path);
```

---

### 5.3 Block Device Interface

From [include/core/fs/block.h](include/core/fs/block.h):

#### Block Device Operations

```c
typedef struct {
    // Read `count` blocks starting from LBA `lba` into `buf`
    int (*read_blocks)(struct block_device* dev, uint64_t lba, size_t count, void* buf);

    // Write `count` blocks starting from LBA `lba` from `buf`
    int (*write_blocks)(struct block_device* dev, uint64_t lba, size_t count, const void* buf);
} block_device_ops_t;
```

#### Block Device Structure

```c
typedef struct block_device {
    char name[32];                // Device name (e.g., "cdrom0")
    uint32_t block_size;          // Size of a single block in bytes (e.g., 2048)
    uint64_t total_blocks;        // Total number of blocks on device
    block_device_ops_t ops;       // Driver operations
    void* private_data;           // For driver's internal use
    bool used;
} block_device_t;
```

#### Registration API

```c
// Initialize block device subsystem (called in kmain)
void block_init(void);

// Register a new block device
int register_block_device(const char* name, uint32_t block_size,
                         uint64_t total_blocks, block_device_ops_t* ops,
                         void* private_data);

// Find a registered block device
block_device_t* find_block_device(const char* name);

// Get array of all block devices (for devfs)
block_device_t* get_block_devices(void);
```

#### Implementation Example

```c
// 1. Define operations
static int my_device_read(block_device_t* dev, uint64_t lba, size_t count, void* buf) {
    my_private_t* priv = (my_private_t*)dev->private_data;

    // Read blocks from hardware
    for (size_t i = 0; i < count; i++) {
        // Read block at LBA (lba + i) into (buf + i * block_size)
    }

    return 0;  // Success
}

static int my_device_write(block_device_t* dev, uint64_t lba, size_t count, const void* buf) {
    // Similar to read
    return 0;
}

static block_device_ops_t my_ops = {
    .read_blocks = my_device_read,
    .write_blocks = my_device_write
};

// 2. Register device
void my_device_init(void) {
    my_private_t* priv = kmalloc(sizeof(my_private_t));
    // ... initialize private data ...

    register_block_device("mydev0", 2048, 1024, &my_ops, priv);
    // Device now appears in /dev/mydev0
}
```

#### Integration with VFS

Block devices automatically appear in `/dev/` via the `block_dev_vfs` layer (see [core/fs/block_dev_vfs.c](core/fs/block_dev_vfs.c)).

**Usage from filesystem**:

```c
// In filesystem mount operation:
block_device_t* bdev = find_block_device(device);
if (!bdev) {
    return -ENODEV;
}

// Read blocks
uint8_t buffer[2048];
int ret = bdev->ops.read_blocks(bdev, 16, 1, buffer);  // Read block 16
```

---

### 5.4 Syscall Implementation

From [core/kernel/nvm/syscalls.c](core/kernel/nvm/syscalls.c):29-150

#### Syscall Handler Signature

```c
int32_t syscall_handler(uint8_t syscall_id, nvm_process_t* proc);
```

#### Implementation Pattern

```c
int32_t syscall_handler(uint8_t syscall_id, nvm_process_t* proc) {
    int32_t result = 0;

    switch(syscall_id) {
        case SYS_MY_SYSCALL: {
            // 1. Check capability
            if (!caps_has_capability(proc, CAP_REQUIRED)) {
                result = -EPERM;
                break;
            }

            // 2. Validate stack depth
            if (proc->sp < num_args) {
                LOG_WARN("Process %d: Stack underflow\n", proc->pid);
                result = -1;
                break;
            }

            // 3. Pop arguments from stack (RIGHT-TO-LEFT)
            int arg2 = proc->stack[proc->sp - 1];  // Last pushed (rightmost arg)
            int arg1 = proc->stack[proc->sp - 2];  // First pushed (leftmost arg)
            proc->sp -= num_args;

            // 4. Extract strings if needed (null-terminated on stack)
            // See SYS_SPAWN in syscalls.c for string extraction example

            // 5. Perform operation
            result = do_operation(arg1, arg2);

            // 6. Return result (will be pushed to stack by caller)
            break;
        }
    }

    return result;
}
```

#### String Extraction from Stack

From [core/kernel/nvm/syscalls.c](core/kernel/nvm/syscalls.c):72-108 (SYS_SPAWN example):

```c
// String is on stack as: [0, 'h', 'e', 'l', 'l', 'o']
// (null terminator at bottom, characters above)

int stack_pos = proc->sp - 1;  // Start at top of stack
int end_pos = stack_pos;
int start_pos = -1;

// Find null terminator
while (stack_pos >= 0) {
    if (proc->stack[stack_pos] == 0) {
        start_pos = stack_pos + 1;  // First character after null
        break;
    }
    stack_pos--;
}

if (start_pos == -1 || start_pos > end_pos) {
    LOG_WARN("Malformed string\n");
    return -EINVAL;
}

// Extract characters
int len = end_pos - start_pos + 1;
char* str = kmalloc(len + 1);
for (int i = 0; i < len; i++) {
    str[i] = (char)proc->stack[start_pos + i];
}
str[len] = '\0';

// Update stack pointer
proc->sp = start_pos - 1;  // Remove string and null from stack
```

#### Adding a New Syscall (Step-by-Step)

See Section 6.1 for detailed workflow.

---

## 6. Development Workflow

### 6.1 Adding a New Syscall

**Steps**:

1. **Define syscall number** in [include/core/kernel/nvm/syscall.h](include/core/kernel/nvm/syscall.h):

```c
#define SYS_MY_NEW_SYSCALL 0x0E
```

2. **Add to syscall table** in [docs/3.1-Syscalls.md](docs/3.1-Syscalls.md):

```markdown
| MY_NEW_SYSCALL | 0x0E | Description of syscall | CAP_REQUIRED |
```

3. **Implement handler** in [core/kernel/nvm/syscalls.c](core/kernel/nvm/syscalls.c):

```c
case SYS_MY_NEW_SYSCALL: {
    // Check capability
    if (!caps_has_capability(proc, CAP_REQUIRED)) {
        result = -EPERM;
        break;
    }

    // Pop arguments
    int arg1 = proc->stack[proc->sp - 1];
    proc->sp--;

    // Perform operation
    result = my_operation(arg1);
    break;
}
```

4. **Write test program** in [samples/](samples/):

```assembly
.NVM0
push 42
syscall my_new_syscall
syscall exit
```

5. **Compile and test**:

```bash
chorus nvma
./tools/nvma/nvma.bin samples/test_my_syscall.asm apps/test.bin
```

6. **Update documentation**:
   - Create [docs/3.X-My-syscall.md](docs/) with detailed description
   - Update this file (CLAUDE.md) if needed

---

### 6.2 Implementing a New Filesystem

**Steps**:

1. **Create header** in [include/core/fs/myfs.h](include/core/fs/):

```c
#ifndef MYFS_H
#define MYFS_H

void myfs_init(void);

#endif
```

2. **Create implementation** in [core/fs/myfs.c](core/fs/):

```c
// SPDX-License-Identifier: GPL-3.0-only

#include <core/fs/vfs.h>
#include <core/fs/block.h>
#include <core/kernel/log.h>

// Private data structure
typedef struct {
    block_device_t* block_device;
    // ... other fields
} myfs_private_t;

// Implement all required operations
static int myfs_mount(vfs_mount_t* mnt, const char* device, void* data) { /* ... */ }
static int myfs_unmount(vfs_mount_t* mnt) { /* ... */ }
static int myfs_open(vfs_mount_t* mnt, const char* path, int flags, vfs_file_handle_t* h) { /* ... */ }
// ... implement all other operations ...

static const vfs_fs_ops_t myfs_ops = {
    .name = "myfs",
    .mount = myfs_mount,
    .unmount = myfs_unmount,
    .open = myfs_open,
    // ... all operations ...
};

void myfs_init(void) {
    vfs_register_filesystem("myfs", &myfs_ops, 0);
    LOG_INFO("MyFS filesystem registered.");
}
```

3. **Add to build system** in [chorus.build](chorus.build):

```yaml
core/fs/myfs:
  deps: []
  cmds:
    - "${CC} ${CFLAGS} ${@}.c -o ${OBJ_DIR}/${@}.o"
```

Add `core/fs/myfs` to `kernel.bin` deps.

4. **Call init in kernel** in [core/kernel/kernel.c](core/kernel/kernel.c):

```c
#include <core/fs/myfs.h>

void kmain() {
    // ... other initialization ...
    myfs_init();
}
```

5. **Mount filesystem**:

```c
vfs_mount_fs("myfs", "/mnt/myfs", "/dev/device", 0, NULL);
```

6. **Document** in [docs/4.X-myfs.md](docs/):

**Reference**: See [core/fs/iso9660.c](core/fs/iso9660.c) for a complete example.

---

### 6.3 Adding a Block Device Driver

**Steps**:

1. **Create header** in [include/core/drivers/mydevice.h](include/core/drivers/):

```c
#ifndef MYDEVICE_H
#define MYDEVICE_H

void mydevice_init(void);

#endif
```

2. **Implement driver** in [core/drivers/mydevice.c](core/drivers/mydevice.c):

```c
// SPDX-License-Identifier: GPL-3.0-only

#include <core/fs/block.h>
#include <core/kernel/mem.h>
#include <core/kernel/log.h>

typedef struct {
    // Driver-specific data
    void* io_base;
    uint32_t irq;
} mydevice_private_t;

static int mydevice_read(block_device_t* dev, uint64_t lba, size_t count, void* buf) {
    mydevice_private_t* priv = (mydevice_private_t*)dev->private_data;

    // Read blocks from hardware
    for (size_t i = 0; i < count; i++) {
        // Read block at LBA (lba + i)
        // Write to (buf + i * dev->block_size)
    }

    return 0;
}

static int mydevice_write(block_device_t* dev, uint64_t lba, size_t count, const void* buf) {
    // Similar to read
    return 0;
}

static block_device_ops_t mydevice_ops = {
    .read_blocks = mydevice_read,
    .write_blocks = mydevice_write
};

void mydevice_init(void) {
    mydevice_private_t* priv = kmalloc(sizeof(mydevice_private_t));
    if (!priv) {
        LOG_ERROR("Failed to allocate driver data\n");
        return;
    }

    // Initialize hardware
    // ...

    // Register with block layer
    int ret = register_block_device("mydev0", 2048, 1024, &mydevice_ops, priv);
    if (ret != 0) {
        LOG_ERROR("Failed to register block device\n");
        kfree(priv);
        return;
    }

    LOG_INFO("MyDevice registered as /dev/mydev0");
}
```

3. **Add to build system** in [chorus.build](chorus.build)

4. **Call init in kernel** in [core/kernel/kernel.c](core/kernel/kernel.c)

5. **Device appears automatically** in `/dev/mydev0`

**Reference**: See [core/drivers/cdrom.c](core/drivers/cdrom.c) for a complete example.

---

### 6.4 Commit Message Convention

From git log analysis:

**Format**:

```
<type>: <description>

[Optional body paragraph explaining the change]
```

**Types**:
- `feat` - New feature
- `fix` - Bug fix
- `refactor` - Code restructuring without behavior change
- `docs` - Documentation updates
- `chore` - Build system, tooling

**Examples** (from git log):

```
feat: Implement Block Device Abstraction Layer
```

```
fix: prevent caching of /proc/meminfo data
```

```
refactor: Introduce mount points and filesystem operations interface
```

```
docs: Update roadmap with VFS refactoring and ext2 plan
```

---

## 7. Common Patterns and Idioms

### 7.1 String Handling

```c
// Safe string copy (prevents overflow)
strcpy_safe(dest, src, sizeof(dest));

// String comparison
if (strcmp(str1, str2) == 0) {
    // Strings are equal
}

// Find character in string
char* pos = strchr(string, '/');
if (pos) {
    // Found '/' in string
}

// String length
size_t len = strlen(str);
```

---

### 7.2 Iteration Patterns

#### Find Free Slot in Array

```c
for (int i = 0; i < MAX_ITEMS; i++) {
    if (!items[i].used) {
        // Found free slot
        items[i].used = true;
        // ... initialize
        return i;
    }
}
return -ENOMEM;  // No free slots
```

#### Search by Name

```c
for (int i = 0; i < MAX_ITEMS; i++) {
    if (items[i].used && strcmp(items[i].name, target_name) == 0) {
        return &items[i];  // Found
    }
}
return NULL;  // Not found
```

---

### 7.3 Device Registration Pattern

Common pattern across all subsystems:

```c
// Static registry
static my_type_t registry[MAX_ITEMS];

// Initialization
void my_subsystem_init(void) {
    memset(registry, 0, sizeof(registry));
    LOG_INFO("Subsystem initialized.");
}

// Registration
int register_my_thing(const char* name, /* ... */) {
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (!registry[i].used) {
            strcpy_safe(registry[i].name, name, sizeof(registry[i].name));
            // ... initialize fields
            registry[i].used = true;
            LOG_INFO("Registered '%s'.", name);
            return 0;
        }
    }
    LOG_WARN("Registry full for '%s'.", name);
    return -ENOMEM;
}

// Lookup
my_type_t* find_my_thing(const char* name) {
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (registry[i].used && strcmp(registry[i].name, name) == 0) {
            return &registry[i];
        }
    }
    return NULL;
}
```

---

## 8. Troubleshooting and Debugging

### 8.1 Build Issues

**Missing includes**:
- Check `-I./include/ -I./` in [chorus.build](chorus.build):7

**Undefined reference**:
- Add target to `kernel.bin` deps in [chorus.build](chorus.build):31-37

**Linker errors**:
- Check [link.ld](link.ld) linker script
- Verify LDFLAGS in [chorus.build](chorus.build):9

**Assembly errors**:
- nasm format must be `elf64`
- Check ASMFLAGS in [chorus.build](chorus.build):8

---

### 8.2 Runtime Debugging

**Serial log**:
- Output written to `qemu.log` file
- Use `tail -f qemu.log` to monitor in real-time

**LOG_DEBUG macros**:
- Add `LOG_DEBUG("Variable: %d\n", var);` throughout code
- Current log level: `LOG_LEVEL_TRACE` (all enabled)

**VFS issues**:
- Check `/var/log/system.log` pseudo-file for VFS messages
- Use `vfs_list()` to dump all registered files

**Process issues**:
- Check `/proc/<pid>/` entries in procfs
- Use `LOG_DEBUG` in [core/kernel/nvm/nvm.c](core/kernel/nvm/nvm.c)

---

### 8.3 NVM Debugging

**Invalid signature**:
- Ensure `.NVM0` at start of bytecode file
- Signature bytes: `0x4E 0x56 0x4D 0x30`

**Stack underflow**:
- Check syscall argument count
- Verify stack operations (PUSH/POP balance)

**Capability errors**:
- Verify process has required `CAP_*` capability
- Use `CAP_CHECK` syscall to query capabilities
- Check `caps_has_capability()` calls in syscall handler

**Process not executing**:
- Check `proc->active` flag
- Verify scheduler is running (`nvm_scheduler_tick()`)
- Check for infinite loops in bytecode

**Syscall failures**:
- Check return value (negative = error)
- Add `LOG_DEBUG` in syscall handler
- Verify arguments on stack

---

## 9. Current State and Roadmap

### 9.1 Completed (v0.2.0)

From [ROADMAP.md](ROADMAP.md):

- **VFS Refactoring** - Mount point architecture implemented
- **Block Device Abstraction Layer** - Unified interface for block devices
- **Internal Shell Refactor** - Improved command handling
- **procfs** - /proc/meminfo, /proc/cpuinfo, /proc/<pid>/
- **devfs** - /dev/null, /dev/zero, /dev/full, /dev/stdin, /dev/stdout, /dev/stderr
- **Capability-based security** - Complete CAPS system
- **NVM with 27 instructions** - Full bytecode VM
- **13 syscalls** - Process, FS, IPC, hardware access
- **iso9660 filesystem** - CD-ROM support
- **ramfs** - In-memory filesystem

---

### 9.2 In Progress

From [ROADMAP.md](ROADMAP.md):

**High Priority**:
- **EXT2 Filesystem Driver** - Initial support for ext2 (CRITICAL for v0.2.0)

**Medium Priority**:
- **/dev/console** - Currently write-only, needs full implementation
- **/sys/pci** - Basic PCI device enumeration (vendor/device without interrupts)

**Low Priority**:
- **Shell removal from kernel** - Move to userspace
- **Nutils**: cat, ls utilities
- **ARGC/ARGV passing** - For binaries launched from internal shell
- **Full init system** - Proper process initialization

---

### 9.3 Future Vision

From [docs/1.2-Our-goals.md](docs/1.2-Our-goals.md):

**By final release**:

1. **Complete Nutils** - Full set of system utilities following NovariaOS logic

2. **Programming Language Support**:
   - Low-level language (C) for system components and drivers
   - High-level scripting language (Python) for automation

3. **Network Stack** (optional but desirable) - Local network and internet access

4. **Legendary Stability** - FreeBSD-level debugging and reliability

5. **Distinct Niche Community** - Respected OS with dedicated followers who use it as their primary system

---

## 10. Quick Reference

### 10.1 Key File Paths

**Critical headers**:
- [include/core/fs/vfs.h](include/core/fs/vfs.h) - VFS interface (lines 127-154: `vfs_fs_ops_t`)
- [include/core/kernel/nvm/nvm.h](include/core/kernel/nvm/nvm.h) - NVM process structure
- [include/core/kernel/nvm/caps.h](include/core/kernel/nvm/caps.h) - Capability definitions
- [include/core/fs/block.h](include/core/fs/block.h) - Block device interface

**Critical implementations**:
- [core/kernel/kernel.c](core/kernel/kernel.c) - Kernel entry point (`kmain()`)
- [core/kernel/nvm/nvm.c](core/kernel/nvm/nvm.c) - NVM execution engine
- [core/kernel/nvm/syscalls.c](core/kernel/nvm/syscalls.c) - Syscall handlers
- [core/fs/vfs.c](core/fs/vfs.c) - VFS implementation

**Documentation**:
- [docs/](docs/) - All technical documentation
- [ROADMAP.md](ROADMAP.md) - Development roadmap
- [chorus.build](chorus.build) - Build configuration

---

### 10.2 Key Constants

From [include/core/kernel/nvm/nvm.h](include/core/kernel/nvm/nvm.h) and [include/core/fs/vfs.h](include/core/fs/vfs.h):

```c
// NVM
#define MAX_PROCESSES 32768    // Maximum processes
#define TIME_SLICE_MS 2        // Scheduler time slice (2ms)
#define MAX_CAPS 16            // Max capabilities per process
#define STACK_SIZE 1024        // Stack size (1024 x 32-bit = 4KB)
#define MAX_LOCALS 512         // Local variables per process

// VFS
#define MAX_FILES 256          // Maximum total files
#define MAX_HANDLES 64         // Maximum open file handles
#define MAX_FILENAME 256       // Maximum filename length
#define MAX_FILE_SIZE 65536    // Maximum file size (64KB)
#define MAX_MOUNTS 32          // Maximum mount points

// Block devices
#define MAX_BLOCK_DEVICES 16   // Maximum block devices
```

---

### 10.3 Quick Commands

```bash
# Build entire project
chorus all

# Build and run in QEMU
chorus all run

# Build NVM assembler
chorus nvma

# Assemble NVM program
./tools/nvma/nvma.bin samples/program.asm output.bin

# Rebuild initramfs only
chorus rebuild-initramfs

# Production release build
chorus build-release

# Monitor serial log
tail -f qemu.log
```

---

## Appendix: Mixed Language Notes

This project contains mixed Russian and English comments and documentation. When working with the codebase:

- **Code comments**: Primarily English
- **Documentation** (docs/): Mixed Russian and English
- **Git commits**: Mixed languages
- **This file**: English for international accessibility

Both AI assistants and human contributors should be comfortable with both languages when reading documentation and commit history.

---

## Document Maintenance

**Last Updated**: 2026-01-11
**Version**: 0.2.0
**Maintainer**: NovariaOS Core Team

**Update this file when**:
- New major features are added
- API interfaces change
- New conventions are established
- Critical file paths change
- Build system is modified

**Keep synchronized with**:
- [docs/](docs/) - Official documentation
- [ROADMAP.md](ROADMAP.md) - Development priorities
- Code comments and inline documentation

---

*This document is part of the NovariaOS project. For more information, see the official documentation in the [docs/](docs/) directory.*
