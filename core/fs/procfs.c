// SPDX-License-Identifier: GPL-3.0-only

#include <core/fs/procfs.h>
#include <core/fs/vfs.h>
#include <core/kernel/vge/fb_render.h>
#include <core/arch/cpuid.h>
#include <core/kernel/kstd.h>
#include <core/kernel/mem.h>
#include <core/kernel/nvm/nvm.h>
#include <stdint.h>
#include <string.h>

static char cpuinfo_buf[2048];
static int cpuinfo_initialized = 0;

// Procfs entry registry for new VFS interface
#define MAX_PROCFS_ENTRIES 64

typedef struct {
    char name[128];
    vfs_dev_read_t read_fn;
    void* data;
    bool used;
    bool is_dir;
} procfs_entry_t;

static procfs_entry_t procfs_entries[MAX_PROCFS_ENTRIES];

static procfs_entry_t* procfs_find_entry(const char* name) {
    for (int i = 0; i < MAX_PROCFS_ENTRIES; i++) {
        if (procfs_entries[i].used && strcmp(procfs_entries[i].name, name) == 0) {
            return &procfs_entries[i];
        }
    }
    return NULL;
}

static void procfs_add_entry(const char* name, vfs_dev_read_t read_fn, void* data, bool is_dir) {
    for (int i = 0; i < MAX_PROCFS_ENTRIES; i++) {
        if (!procfs_entries[i].used) {
            strcpy(procfs_entries[i].name, name);
            procfs_entries[i].read_fn = read_fn;
            procfs_entries[i].data = data;
            procfs_entries[i].is_dir = is_dir;
            procfs_entries[i].used = true;
            return;
        }
    }
}

static void procfs_remove_entry(const char* name) {
    for (int i = 0; i < MAX_PROCFS_ENTRIES; i++) {
        if (procfs_entries[i].used && strcmp(procfs_entries[i].name, name) == 0) {
            procfs_entries[i].used = false;
            return;
        }
    }
}

// ProcFS operations for new VFS interface
static int procfs_mount(vfs_mount_t* mnt, const char* device, void* data) {
    (void)device; (void)data;
    mnt->fs_private = NULL;
    return 0;
}

static int procfs_unmount(vfs_mount_t* mnt) {
    (void)mnt;
    return 0;
}

static int procfs_stat(vfs_mount_t* mnt, const char* path, vfs_stat_t* stat) {
    (void)mnt;

    // Root directory
    if (path[0] == '\0' || strcmp(path, "/") == 0) {
        stat->st_mode = VFS_S_IFDIR | 0555;
        stat->st_size = 0;
        stat->st_blksize = 512;
        stat->st_mtime = 0;
        return 0;
    }

    // Look for entry
    procfs_entry_t* entry = procfs_find_entry(path);
    if (entry) {
        if (entry->is_dir) {
            stat->st_mode = VFS_S_IFDIR | 0555;
        } else {
            stat->st_mode = VFS_S_IFREG | 0444;
        }
        stat->st_size = 0;
        stat->st_blksize = 512;
        stat->st_mtime = 0;
        return 0;
    }

    return -ENOENT;
}

static int procfs_readdir_impl(vfs_mount_t* mnt, const char* path, vfs_dirent_t* entries, size_t max) {
    (void)mnt;

    int count = 0;
    size_t path_len = strlen(path);

    for (int i = 0; i < MAX_PROCFS_ENTRIES && (size_t)count < max; i++) {
        if (!procfs_entries[i].used) continue;

        const char* entry_name = procfs_entries[i].name;
        size_t entry_len = strlen(entry_name);

        // Check if entry is direct child of path
        bool is_child = false;

        if (path_len == 0 || (path_len == 1 && path[0] == '/')) {
            // Root - look for top-level entries (no slash or single leading slash then no more)
            int slashes = 0;
            for (size_t j = 0; j < entry_len; j++) {
                if (entry_name[j] == '/') slashes++;
            }
            is_child = (slashes == 0);
        } else {
            // Check if entry starts with path/ and has no more slashes
            if (entry_len > path_len &&
                strncmp(entry_name, path, path_len) == 0 &&
                entry_name[path_len] == '/') {
                int slashes = 0;
                for (size_t j = path_len + 1; j < entry_len; j++) {
                    if (entry_name[j] == '/') slashes++;
                }
                is_child = (slashes == 0);
            }
        }

        if (is_child) {
            const char* basename = entry_name;
            if (path_len > 0) {
                basename = entry_name + path_len;
                if (*basename == '/') basename++;
            }
            strcpy(entries[count].d_name, basename);
            entries[count].d_type = procfs_entries[i].is_dir ? VFS_TYPE_DIR : VFS_TYPE_FILE;
            count++;
        }
    }

    return count;
}

static const vfs_fs_ops_t procfs_ops = {
    .name = "procfs",
    .mount = procfs_mount,
    .unmount = procfs_unmount,
    .stat = procfs_stat,
    .readdir = procfs_readdir_impl,
    // Other operations use legacy mechanism via vfs_pseudo_register
    .open = NULL,
    .close = NULL,
    .read = NULL,
    .write = NULL,
    .seek = NULL,
    .mkdir = NULL,
    .rmdir = NULL,
    .unlink = NULL,
    .ioctl = NULL,
    .sync = NULL,
};

static vfs_ssize_t procfs_bytecode_read(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos) {
    nvm_process_t* process = (nvm_process_t*)file->dev_data;
    if (process == NULL) return -1;
    
    char bytecode_buf[8192];
    int bytecode_initialized = 0;
    
    if (!bytecode_initialized) {
        char *ptr = bytecode_buf;
        size_t remaining = sizeof(bytecode_buf);
        
        strcpy_safe(ptr, "Bytecode (hex):\n", remaining);
        ptr += strlen(ptr);
        remaining = sizeof(bytecode_buf) - (ptr - bytecode_buf);
        
        char ascii_part[17] = {0};
        int ascii_idx = 0;
        
        for (size_t i = 0; i < process->size; i++) {
            if (i > 0 && i % 16 == 0) {
                strcat_safe(ptr, "  |", remaining);
                for (int j = 0; j < 16; j++) {
                    char c = ascii_part[j];
                    if (c >= 32 && c <= 126) {
                        char tmp[2] = {c, '\0'};
                        strcat_safe(ptr, tmp, remaining);
                    } else {
                        strcat_safe(ptr, ".", remaining);
                    }
                }
                strcat_safe(ptr, "|\n", remaining);
                size_t written = strlen(ptr) - (ptr - bytecode_buf);
                ptr = bytecode_buf + written;
                remaining = sizeof(bytecode_buf) - written;
                remaining = sizeof(bytecode_buf) - (ptr - bytecode_buf);
                
                memset(ascii_part, 0, sizeof(ascii_part));
                ascii_idx = 0;
            }
            else if (i > 0 && i % 8 == 0) {
                strcat_safe(ptr, " ", remaining);
                ptr += 1;
                remaining -= 1;
            }
            
            uint8_t byte = process->bytecode[i];

            char hex_byte[3];
            const char* hex_chars = "0123456789abcdef";
            hex_byte[0] = hex_chars[(byte >> 4) & 0x0F];
            hex_byte[1] = hex_chars[byte & 0x0F];
            hex_byte[2] = '\0';
            
            strcat_safe(ptr, hex_byte, remaining);
            strcat_safe(ptr, " ", remaining);
            ptr += 3;
            remaining -= 3;
            
            ascii_part[ascii_idx++] = byte;
            
            if (remaining <= 50) {
                if (ascii_idx > 0) {
                    while (i % 16 != 15 && i < process->size - 1) {
                        strcat_safe(ptr, "   ", remaining);
                        i++;
                        ptr += 3;
                        remaining -= 3;
                    }
                    strcat_safe(ptr, "  |", remaining);
                    for (int j = 0; j < ascii_idx; j++) {
                        char c = ascii_part[j];
                        if (c >= 32 && c <= 126) {
                            char tmp[2] = {c, '\0'};
                            strcat_safe(ptr, tmp, remaining);
                        } else {
                            strcat_safe(ptr, ".", remaining);
                        }
                    }
                    strcat_safe(ptr, "|\n", remaining);
                }
                strcat_safe(ptr, "\n...[truncated]", remaining);
                break;
            }
        }

        if (ascii_idx > 0) {
            while (ascii_idx < 16) {
                strcat_safe(ptr, "   ", remaining);
                ptr += 3;
                remaining -= 3;
                ascii_idx++;
            }
            strcat_safe(ptr, "  |", remaining);
            for (int j = 0; j < 16 && ascii_part[j] != 0; j++) {
                char c = ascii_part[j];
                if (c >= 32 && c <= 126) {
                    char tmp[2] = {c, '\0'};
                    strcat_safe(ptr, tmp, remaining);
                } else {
                    strcat_safe(ptr, ".", remaining);
                }
            }
            strcat_safe(ptr, "|\n", remaining);
        }
        
        strcat_safe(ptr, "\nBytecode size: ", remaining);
        char size_str[16];
        itoa(process->size, size_str, 10);
        strcat_safe(ptr, size_str, remaining);
        strcat_safe(ptr, " bytes\n", remaining);
        
        bytecode_initialized = 1;
    }
    
    size_t len = strlen(bytecode_buf);
    if (*pos >= len) return 0;
    
    size_t remaining = len - *pos;
    size_t to_copy = (remaining < count) ? remaining : count;
    
    memcpy(buf, bytecode_buf + *pos, to_copy);
    *pos += to_copy;
    
    return to_copy;
}

static vfs_ssize_t procfs_status_read(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos) {
    char status_buf[512];
    int status_initialized = 0;
    
    if (!status_initialized) {
        nvm_process_t* process = (nvm_process_t*)file->dev_data;
        if (process == NULL) return -1;
        
        char pid_str[16];
        char sp_str[16];
        char ip_str[16];
        char size_str[16];
        char exit_str[16];
        
        itoa(process->pid, pid_str, 10);
        itoa(process->sp, sp_str, 10);
        itoa(process->ip, ip_str, 10);
        itoa(process->size, size_str, 10);
        itoa(process->exit_code, exit_str, 10);
        
        strcpy_safe(status_buf, "pid: ", sizeof(status_buf));
        strcat_safe(status_buf, pid_str, sizeof(status_buf));
        strcat_safe(status_buf, "\nactive: ", sizeof(status_buf));
        strcat_safe(status_buf, process->active ? "yes" : "no", sizeof(status_buf));
        strcat_safe(status_buf, "\nblocked: ", sizeof(status_buf));
        strcat_safe(status_buf, process->blocked ? "yes" : "no", sizeof(status_buf));
        strcat_safe(status_buf, "\nsp: ", sizeof(status_buf));
        strcat_safe(status_buf, sp_str, sizeof(status_buf));
        strcat_safe(status_buf, "\nip: ", sizeof(status_buf));
        strcat_safe(status_buf, ip_str, sizeof(status_buf));
        strcat_safe(status_buf, "\nsize: ", sizeof(status_buf));
        strcat_safe(status_buf, size_str, sizeof(status_buf));
        strcat_safe(status_buf, "\nexit_code: ", sizeof(status_buf));
        strcat_safe(status_buf, exit_str, sizeof(status_buf));
        strcat_safe(status_buf, "\nwakeup_reason: ", sizeof(status_buf));
        itoa(process->wakeup_reason, pid_str, 10);
        strcat_safe(status_buf, pid_str, sizeof(status_buf));
        strcat_safe(status_buf, "\ncaps_count: ", sizeof(status_buf));
        itoa(process->caps_count, pid_str, 10);
        strcat_safe(status_buf, pid_str, sizeof(status_buf));
        strcat_safe(status_buf, "\n", sizeof(status_buf));
        
        status_initialized = 1;
    }
    
    size_t len = strlen(status_buf);
    if (*pos >= len) return 0;
    
    size_t remaining = len - *pos;
    size_t to_copy = (remaining < count) ? remaining : count;
    
    memcpy(buf, status_buf + *pos, to_copy);
    *pos += to_copy;
    
    return to_copy;
}

static vfs_ssize_t procfs_stack_read(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos) {
    nvm_process_t* process = (nvm_process_t*)file->dev_data;
    if (process == NULL) return -1;
    
    char stack_buf[4096];
    int stack_initialized = 0;
    
    if (!stack_initialized) {
        char *ptr = stack_buf;
        size_t remaining = sizeof(stack_buf);
        
        strcpy_safe(ptr, "Stack dump (hex):\n", remaining);
        ptr += strlen(ptr);
        remaining = sizeof(stack_buf) - (ptr - stack_buf);
        
        for (int i = 0; i < process->sp; i++) {
            if (i > 0 && i % 8 == 0) {
                strcat_safe(ptr, "\n", remaining);
                ptr += 1;
                remaining--;
            }
            
            char hex_str[9];
            itoa(process->stack[i], hex_str, 16);

            char formatted[11];
            strcpy_safe(formatted, "0x", sizeof(formatted));

            int len = strlen(hex_str);
            for (int j = 0; j < 8 - len; j++) {
                strcat_safe(formatted, "0", sizeof(formatted));
            }
            strcat_safe(formatted, hex_str, sizeof(formatted));
            strcat_safe(formatted, " ", sizeof(formatted));
            
            strcat_safe(ptr, formatted, remaining);
            ptr += strlen(formatted);
            remaining = sizeof(stack_buf) - (ptr - stack_buf);
            
            if (remaining <= 20) {
                break;
            }
        }
        
        strcat_safe(ptr, "\n", remaining);
        stack_initialized = 1;
    }
    
    size_t len = strlen(stack_buf);
    if (*pos >= len) return 0;
    
    size_t remaining = len - *pos;
    size_t to_copy = (remaining < count) ? remaining : count;
    
    memcpy(buf, stack_buf + *pos, to_copy);
    *pos += to_copy;
    
    return to_copy;
}

void procfs_init(void) {
    // Initialize entry registry
    for (int i = 0; i < MAX_PROCFS_ENTRIES; i++) {
        procfs_entries[i].used = false;
    }

    // Register procfs as a filesystem
    vfs_register_filesystem("procfs", &procfs_ops, VFS_FS_NODEV | VFS_FS_VIRTUAL | VFS_FS_READONLY);
    vfs_mount_fs("procfs", "/proc", NULL, VFS_MNT_READONLY, NULL);

    // Register entries in new registry (for readdir/stat)
    procfs_add_entry("cpuinfo", procfs_cpuinfo, NULL, false);
    procfs_add_entry("meminfo", procfs_meminfo, NULL, false);
    procfs_add_entry("pci", procfs_pci, NULL, false);
    procfs_add_entry("uptime", procfs_uptime, NULL, false);

    // Register via legacy mechanism (for actual I/O)
    vfs_pseudo_register("/proc/cpuinfo", procfs_cpuinfo, NULL, NULL, NULL, NULL);
    vfs_pseudo_register("/proc/meminfo", procfs_meminfo, NULL, NULL, NULL, NULL);
    vfs_pseudo_register("/proc/pci", procfs_pci, NULL, NULL, NULL, NULL);
    vfs_pseudo_register("/proc/uptime", procfs_uptime, NULL, NULL, NULL, NULL);
    vfs_pseudo_register("/proc/version", procfs_version, NULL, NULL, NULL, NULL);

    cpuinfo_init();
}

void procfs_register(int pid, void* process_data) {
    char pid_str[16];
    char path[64];
    char filepath[128];
    char relpath[128];

    itoa(pid, pid_str, 10);

    strcpy(path, "/proc/");
    strcat(path, pid_str);

    // Register in new entry registry (relative paths without /proc/)
    procfs_add_entry(pid_str, NULL, process_data, true);

    strcpy(relpath, pid_str);
    strcat(relpath, "/status");
    procfs_add_entry(relpath, procfs_status_read, process_data, false);

    strcpy(relpath, pid_str);
    strcat(relpath, "/stack");
    procfs_add_entry(relpath, procfs_stack_read, process_data, false);

    strcpy(relpath, pid_str);
    strcat(relpath, "/bytecode");
    procfs_add_entry(relpath, procfs_bytecode_read, process_data, false);

    // Register via legacy mechanism
    vfs_mkdir(path);

    strcpy(filepath, path);
    strcat(filepath, "/status");
    vfs_pseudo_register(filepath, procfs_status_read, NULL, NULL, NULL, process_data);

    strcpy(filepath, path);
    strcat(filepath, "/stack");
    vfs_pseudo_register(filepath, procfs_stack_read, NULL, NULL, NULL, process_data);

    strcpy(filepath, path);
    strcat(filepath, "/bytecode");
    vfs_pseudo_register(filepath, procfs_bytecode_read, NULL, NULL, NULL, process_data);
}

void procfs_unregister(int pid) {
    char pid_str[16];
    char path[64];
    char filepath[128];
    char relpath[128];

    itoa(pid, pid_str, 10);

    strcpy(path, "/proc/");
    strcat(path, pid_str);

    // Remove from new entry registry
    strcpy(relpath, pid_str);
    strcat(relpath, "/status");
    procfs_remove_entry(relpath);

    strcpy(relpath, pid_str);
    strcat(relpath, "/stack");
    procfs_remove_entry(relpath);

    strcpy(relpath, pid_str);
    strcat(relpath, "/bytecode");
    procfs_remove_entry(relpath);

    procfs_remove_entry(pid_str);

    // Remove via legacy mechanism
    strcpy(filepath, path);
    strcat(filepath, "/status");
    vfs_delete(filepath);

    strcpy(filepath, path);
    strcat(filepath, "/stack");
    vfs_delete(filepath);

    strcpy(filepath, path);
    strcat(filepath, "/bytecode");
    vfs_delete(filepath);

    vfs_rmdir(path);
}

vfs_ssize_t procfs_cpuinfo(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos) {
    if (!cpuinfo_initialized) {
        cpuinfo_init();
        cpuinfo_initialized = 1;
    }

    size_t len = strlen(cpuinfo_buf);
    if (*pos >= len) {
        return 0;
    }

    size_t remaining = len - *pos;
    size_t to_copy = (remaining < count) ? remaining : count;

    memcpy(buf, cpuinfo_buf + *pos, to_copy);
    *pos += to_copy;

    return to_copy;
}

vfs_ssize_t procfs_meminfo(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos) {
    char meminfo_buf[512];

    // Always recalculate memory info
    size_t memTotal = getMemTotal();
    size_t buddyFree = getMemFree();  // free blocks in buddy allocator
    size_t allocated = getMemUsed();  // memory allocated via kmalloc
    size_t memUsed = allocated;
    size_t memFree = memTotal - allocated;  // available memory for allocation

    size_t overhead = 0; // Buddy allocator doesn't have per-block overhead like linked list

    char total_str[32], used_str[32], free_str[32], ovh_str[32], buddy_free_str[32];
    formatMemorySize(memTotal, total_str);
    formatMemorySize(memUsed, used_str);
    formatMemorySize(memFree, free_str);
    formatMemorySize(overhead, ovh_str);
    formatMemorySize(buddyFree, buddy_free_str);

    strcpy_safe(meminfo_buf, "MemTotal       : ", sizeof(meminfo_buf));
    strcat_safe(meminfo_buf, total_str, sizeof(meminfo_buf));
    strcat_safe(meminfo_buf, "\nMemUsed        : ", sizeof(meminfo_buf));
    strcat_safe(meminfo_buf, used_str, sizeof(meminfo_buf));
    strcat_safe(meminfo_buf, "\nMemFree        : ", sizeof(meminfo_buf));
    strcat_safe(meminfo_buf, free_str, sizeof(meminfo_buf));
    strcat_safe(meminfo_buf, "\n", sizeof(meminfo_buf));

    size_t len = strlen(meminfo_buf);
    if (*pos >= len) {
        return 0;
    }

    size_t remaining = len - *pos;
    size_t to_copy = (remaining < count) ? remaining : count;

    memcpy(buf, meminfo_buf + *pos, to_copy);
    *pos += to_copy;

    return to_copy;
}

vfs_ssize_t procfs_pci(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos) {
    return 0;
}

vfs_ssize_t procfs_uptime(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos) {
    return 0;
}

vfs_ssize_t procfs_version(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos) {
    const char* version_str = "n300326";
    size_t len = strlen(version_str);

    if (*pos >= len) {
        return 0;
    }

    size_t remaining = len - *pos;
    size_t to_copy = (remaining < count) ? remaining : count;

    memcpy(buf, version_str + *pos, to_copy);

    *pos += to_copy;

    return to_copy;
}

int parse_frequency_mhz(const char* str) {
    int integer_part = 0;
    int fractional_part = 0;
    int fractional_digits = 0;
    int in_fraction = 0;

    while (*str == ' ' || *str == '\t') {
        str++;
    }

    while (*str >= '0' && *str <= '9') {
        integer_part = integer_part * 10 + (*str - '0');
        str++;
    }

    if (*str == '.') {
        str++;
        in_fraction = 1;
        while (*str >= '0' && *str <= '9') {
            fractional_part = fractional_part * 10 + (*str - '0');
            fractional_digits++;
            str++;
        }
    }

    int mhz = integer_part * 1000;

    if (fractional_digits > 0) {
        int multiplier = 1;
        for (int i = 0; i < 3 - fractional_digits; i++) {
            multiplier *= 10;
        }
        mhz += fractional_part * multiplier;
    }

    return mhz;
}

void cpuinfo_init(void) {
    char *buf = cpuinfo_buf;
    size_t remaining = sizeof(cpuinfo_buf);
    cpuid_result_t result;
    char num_str[32];
    char brand_str[49] = {0};
    char model_name[64] = {0};
    char mhz_str[32] = "unknown";

    cpuid(0, 0, &result);
    char vendor[13];
    memcpy(vendor, &result.ebx, 4);
    memcpy(vendor + 4, &result.edx, 4);
    memcpy(vendor + 8, &result.ecx, 4);
    vendor[12] = '\0';

    strcpy_safe(buf, "vendor_id       : ", remaining);
    strcat_safe(buf, vendor, remaining);
    strcat_safe(buf, "\n", remaining);
    buf += strlen(buf);
    remaining = sizeof(cpuinfo_buf) - (buf - cpuinfo_buf);

    cpuid(1, 0, &result);

    strcpy_safe(buf, "cpu family      : ", remaining);
    itoa((result.eax >> 8) & 0xF, num_str, 10);
    strcat_safe(buf, num_str, remaining);
    strcat_safe(buf, "\n", remaining);
    buf += strlen(buf);
    remaining = sizeof(cpuinfo_buf) - (buf - cpuinfo_buf);

    strcpy_safe(buf, "model           : ", remaining);
    uint8_t model = (result.eax >> 4) & 0xF;
    uint8_t extended_model = (result.eax >> 16) & 0xF;
    itoa((extended_model << 4) | model, num_str, 10);
    strcat_safe(buf, num_str, remaining);
    strcat_safe(buf, "\n", remaining);
    buf += strlen(buf);
    remaining = sizeof(cpuinfo_buf) - (buf - cpuinfo_buf);

    strcpy_safe(buf, "model name      : ", remaining);

    cpuid(0x80000000, 0, &result);
    if (result.eax >= 0x80000004) {
        cpuid(0x80000002, 0, &result);
        memcpy(brand_str, &result.eax, 4);
        memcpy(brand_str + 4, &result.ebx, 4);
        memcpy(brand_str + 8, &result.ecx, 4);
        memcpy(brand_str + 12, &result.edx, 4);
        
        cpuid(0x80000003, 0, &result);
        memcpy(brand_str + 16, &result.eax, 4);
        memcpy(brand_str + 20, &result.ebx, 4);
        memcpy(brand_str + 24, &result.ecx, 4);
        memcpy(brand_str + 28, &result.edx, 4);
        
        cpuid(0x80000004, 0, &result);
        memcpy(brand_str + 32, &result.eax, 4);
        memcpy(brand_str + 36, &result.ebx, 4);
        memcpy(brand_str + 40, &result.ecx, 4);
        memcpy(brand_str + 44, &result.edx, 4);
        brand_str[48] = '\0';

        int j = 0;
        int last_char_was_space = 0;
        for (int i = 0; i < 48; i++) {
            if (brand_str[i] == ' ') {
                if (!last_char_was_space && j > 0) {
                    model_name[j++] = ' ';
                    last_char_was_space = 1;
                }
            } else if (brand_str[i] != 0) {
                model_name[j++] = brand_str[i];
                last_char_was_space = 0;
            }
        }
        model_name[j] = '\0';
        
        if (strlen(model_name) > 0) {
            strcat_safe(buf, model_name, remaining);
            
            char *ghz_ptr = strstr(model_name, "@");
            if (ghz_ptr) {
                ghz_ptr++;
                while (*ghz_ptr == ' ') ghz_ptr++;

                if ((*ghz_ptr >= '0' && *ghz_ptr <= '9') || *ghz_ptr == '.') {
                    char freq_buf[32];
                    int k = 0;

                    while ((*ghz_ptr >= '0' && *ghz_ptr <= '9') || *ghz_ptr == '.') {
                        freq_buf[k++] = *ghz_ptr++;
                    }
                    freq_buf[k] = '\0';

                    int mhz = parse_frequency_mhz(freq_buf);
                    if (mhz > 0) {
                        itoa(mhz, mhz_str, 10);
                        strcat_safe(mhz_str, ".0", sizeof(mhz_str));
                    }
                }
            }
        } else {
            strcat_safe(buf, "Unknown", remaining);
        }
    } else {
        strcat_safe(buf, "Unknown", remaining);
    }
    strcat_safe(buf, "\n", remaining);
    buf += strlen(buf);
    remaining = sizeof(cpuinfo_buf) - (buf - cpuinfo_buf);

    cpuid(1, 0, &result);
    strcpy_safe(buf, "stepping        : ", remaining);
    itoa(result.eax & 0xF, num_str, 10);
    strcat_safe(buf, num_str, remaining);
    strcat_safe(buf, "\n", remaining);
    buf += strlen(buf);
    remaining = sizeof(cpuinfo_buf) - (buf - cpuinfo_buf);

    strcpy_safe(buf, "cpu MHz         : ", remaining);

    cpuid(0x16, 0, &result);
    if (result.eax != 0 && result.ebx != 0 && result.ecx != 0) {
        itoa(result.eax, num_str, 10);
        strcat_safe(buf, num_str, remaining);
        strcat_safe(buf, ".", remaining);
        itoa(result.ebx, num_str, 10);
        strcat_safe(buf, num_str, remaining);
    } 
    else if (strcmp(mhz_str, "unknown") != 0) {
        strcat_safe(buf, mhz_str, remaining);
    }
    strcat_safe(buf, "\n", remaining);
    buf += strlen(buf);
    remaining = sizeof(cpuinfo_buf) - (buf - cpuinfo_buf);

    cpuid(1, 0, &result);
    strcpy_safe(buf, "fpu             : ", remaining);
    strcat_safe(buf, (result.edx & (1 << 0)) ? "yes" : "no", remaining);
    strcat_safe(buf, "\n", remaining);
}