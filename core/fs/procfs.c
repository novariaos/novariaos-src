// SPDX-License-Identifier: GPL-3.0-only

#include <core/fs/procfs.h>
#include <core/fs/vfs.h>
#include <core/kernel/vge/fb_render.h>
#include <core/arch/cpuid.h>
#include <core/kernel/kstd.h>
#include <core/kernel/mem.h>
#include <core/kernel/rvemu/rvemu.h>
#include <core/drivers/timer.h>
#include <stdint.h>
#include <string.h>

static char cpuinfo_buf[2048];
static int cpuinfo_initialized = 0;

#define MAX_PROCFS_ENTRIES 64
#define MAX_ARGS_PER_PROCESS 32
#define MAX_ARG_LEN 256

typedef struct {
    char name[128];
    vfs_dev_read_t read_fn;
    void* data;
    bool used;
    bool is_dir;
} procfs_entry_t;

typedef struct {
    int pid;
    char* args[MAX_ARGS_PER_PROCESS];
    int argc;
    bool used;
} procfs_args_t;

static procfs_entry_t procfs_entries[MAX_PROCFS_ENTRIES];
static procfs_args_t procfs_args[RV64_MAX_PROCESSES];

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

    if (path[0] == '\0' || strcmp(path, "/") == 0) {
        stat->st_mode = VFS_S_IFDIR | 0555;
        stat->st_size = 0;
        stat->st_blksize = 512;
        stat->st_mtime = 0;
        return 0;
    }

    if (strcmp(path, "self") == 0) {
        char pid_str[16];
        itoa(current_process, pid_str, 10);
        procfs_entry_t* entry = procfs_find_entry(pid_str);
        if (entry && entry->is_dir) {
            stat->st_mode = VFS_S_IFDIR | 0555;
            stat->st_size = 0;
            stat->st_blksize = 512;
            stat->st_mtime = 0;
            return 0;
        }
        return -ENOENT;
    }

    if (strncmp(path, "self/", 5) == 0) {
        char resolved[64];
        char pid_str[16];
        itoa(current_process, pid_str, 10);
        strcpy(resolved, pid_str);
        strcat(resolved, path + 4);
        return procfs_stat(mnt, resolved, stat);
    }

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

    const char* effective_path = path;
    char resolved[64];
    bool self_redirect = false;

    if (strcmp(path, "self") == 0) {
        char pid_str[16];
        itoa(current_process, pid_str, 10);
        strcpy(resolved, pid_str);
        effective_path = resolved;
        self_redirect = true;
    } else if (strncmp(path, "self/", 5) == 0) {
        char pid_str[16];
        itoa(current_process, pid_str, 10);
        strcpy(resolved, pid_str);
        strcat(resolved, path + 4);
        effective_path = resolved;
        self_redirect = true;
    }

    int count = 0;
    size_t path_len = strlen(effective_path);

    for (int i = 0; i < MAX_PROCFS_ENTRIES && (size_t)count < max; i++) {
        if (!procfs_entries[i].used) continue;

        const char* entry_name = procfs_entries[i].name;
        size_t entry_len = strlen(entry_name);

        bool is_child = false;

        if (path_len == 0 || (path_len == 1 && effective_path[0] == '/')) {
            int slashes = 0;
            for (size_t j = 0; j < entry_len; j++) {
                if (entry_name[j] == '/') slashes++;
            }
            is_child = (slashes == 0);
        } else {
            if (entry_len > path_len &&
                strncmp(entry_name, effective_path, path_len) == 0 &&
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

    if (!self_redirect && (path_len == 0 || (path_len == 1 && effective_path[0] == '/'))) {
        if (count < max) {
            strcpy(entries[count].d_name, "self");
            entries[count].d_type = VFS_TYPE_DIR;
            count++;
        }
    }

    return count;
}

typedef struct {
    procfs_entry_t* entry;
    void* dev_data;
    vfs_off_t pos;
} procfs_private_t;

static int procfs_open(vfs_mount_t* mnt, const char* path, int flags, vfs_file_handle_t* h) {
    (void)mnt; (void)flags;
    
    if (path[0] == '\0' || strcmp(path, "/") == 0) {
        return -EISDIR;
    }

    if (strcmp(path, "self") == 0) {
        return -EISDIR;
    }

    char lookup[64];
    if (strncmp(path, "self/", 5) == 0) {
        char pid_str[16];
        itoa(current_process, pid_str, 10);
        strcpy(lookup, pid_str);
        strcat(lookup, path + 4);
    } else {
        strcpy(lookup, path);
    }

    procfs_entry_t* entry = procfs_find_entry(lookup);
    if (!entry || entry->is_dir) {
        return -ENOENT;
    }
    
    procfs_private_t* priv = (procfs_private_t*)kmalloc(sizeof(procfs_private_t));
    if (!priv) return -ENOMEM;
    
    priv->entry = entry;
    priv->pos = 0;
    h->private_data = priv;

    // NOTE: its probably bad practice
    static vfs_file_t temp_file;
    temp_file.dev_data = entry->data;
    temp_file.ops.read = entry->read_fn;
    h->file = &temp_file;
    
    return 0;
}

static int procfs_close(vfs_mount_t* mnt, vfs_file_handle_t* h) {
    (void)mnt;
    if (h->private_data) {
        kfree(h->private_data);
    }
    return 0;
}

static vfs_ssize_t procfs_read(vfs_mount_t* mnt, vfs_file_handle_t* h, void* buf, size_t count) {
    (void)mnt;
    procfs_private_t* priv = (procfs_private_t*)h->private_data;
    
    if (!priv || !priv->entry || !priv->entry->read_fn) {
        return -EACCES;
    }
    
    return priv->entry->read_fn((vfs_file_t*)h->file, buf, count, &priv->pos);
}

static const vfs_fs_ops_t procfs_ops = {
    .name = "procfs",
    .mount = procfs_mount,
    .unmount = procfs_unmount,
    .open = procfs_open,
    .close = procfs_close,
    .read = procfs_read,
    .write = NULL,
    .seek = NULL,
    .stat = procfs_stat,
    .readdir = procfs_readdir_impl,
    .mkdir = NULL,
    .rmdir = NULL,
    .unlink = NULL,
    .ioctl = NULL,
    .sync = NULL,
};

static int parse_frequency_mhz(const char* str) {
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

static vfs_ssize_t procfs_registers_read(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos) {
    rv64_process_t* process = (rv64_process_t*)file->dev_data;
    if (process == NULL) return -1;
    
    char regs_buf[2048];
    char* ptr = regs_buf;
    size_t remaining = sizeof(regs_buf);
    
    strcpy_safe(ptr, "General purpose registers (hex):\n", remaining);
    ptr += strlen(ptr);
    remaining = sizeof(regs_buf) - (ptr - regs_buf);
    
    for (int i = 0; i <= 31 && remaining > 0; i++) {
        char hex_str[20];
        char index_str[8];
        char line[32];
        
        itoa((int)process->cpu.registers[i], hex_str, 16);
        itoa(i, index_str, 10);
        
        strcpy_safe(line, "x", sizeof(line));
        strcat_safe(line, index_str, sizeof(line));
        strcat_safe(line, "=0x", sizeof(line));
        strcat_safe(line, hex_str, sizeof(line));
        strcat_safe(line, "\n", sizeof(line));
        
        strcat_safe(ptr, line, remaining);
        ptr += strlen(line);
        remaining = sizeof(regs_buf) - (ptr - regs_buf);
    }
    
    if (remaining > 0) {
        *ptr = '\0';
    } else {
        regs_buf[sizeof(regs_buf) - 1] = '\0';
    }
    
    size_t len = strlen(regs_buf);
    if (*pos >= len) return 0;
    
    size_t remaining_bytes = len - *pos;
    size_t to_copy = (remaining_bytes < count) ? remaining_bytes : count;
    
    memcpy(buf, regs_buf + *pos, to_copy);
    *pos += to_copy;
    
    return to_copy;
}

static vfs_ssize_t procfs_status_read(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos) {
    rv64_process_t* process = (rv64_process_t*)file->dev_data;
    if (process == NULL) return -1;
    
    char status_buf[512];
    char pid_str[16];
    char pc_str[16];
    char sp_str[16];
    char size_str[16];
    char exit_str[16];
    
    itoa((int)process->pid, pid_str, 10);
    itoa((int)process->cpu.pc, pc_str, 16);
    itoa((int)process->cpu.registers[2], sp_str, 16);
    itoa((int)process->memory.size, size_str, 10);
    itoa(process->exit_code, exit_str, 10);
    
    strcpy_safe(status_buf, "pid: ", sizeof(status_buf));
    strcat_safe(status_buf, pid_str, sizeof(status_buf));
    strcat_safe(status_buf, "\nactive: ", sizeof(status_buf));
    strcat_safe(status_buf, (process->cpu.halted == 0) ? "yes" : "no", sizeof(status_buf));
    strcat_safe(status_buf, "\npc (hex): ", sizeof(status_buf));
    strcat_safe(status_buf, pc_str, sizeof(status_buf));
    strcat_safe(status_buf, "\nsp (hex): ", sizeof(status_buf));
    strcat_safe(status_buf, sp_str, sizeof(status_buf));
    strcat_safe(status_buf, "\nmemory size: ", sizeof(status_buf));
    strcat_safe(status_buf, size_str, sizeof(status_buf));
    strcat_safe(status_buf, "\nexit_code: ", sizeof(status_buf));
    strcat_safe(status_buf, exit_str, sizeof(status_buf));
    strcat_safe(status_buf, "\n", sizeof(status_buf));
    
    size_t len = strlen(status_buf);
    if (*pos >= len) return 0;
    
    size_t remaining = len - *pos;
    size_t to_copy = (remaining < count) ? remaining : count;
    
    memcpy(buf, status_buf + *pos, to_copy);
    *pos += to_copy;
    
    return to_copy;
}

static vfs_ssize_t procfs_args_read(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos) {
    rv64_process_t* process = (rv64_process_t*)file->dev_data;
    if (process == NULL) return -1;
    
    procfs_args_t* args = NULL;
    for (int i = 0; i < RV64_MAX_PROCESSES; i++) {
        if (procfs_args[i].used && procfs_args[i].pid == (int)process->pid) {
            args = &procfs_args[i];
            break;
        }
    }
    
    if (!args || args->argc == 0) {
        return 0;
    }
    
    char args_buf[4096];
    char *ptr = args_buf;
    size_t remaining = sizeof(args_buf);
    
    for (int i = 0; i < args->argc && remaining > 0; i++) {
        if (args->args[i]) {
            strcpy_safe(ptr, args->args[i], remaining);
            size_t len = strlen(args->args[i]);
            ptr += len;
            remaining -= len;
        }
        
        if (remaining > 1 && i < args->argc - 1) {
            *ptr = '\n';
            ptr++;
            remaining--;
            *ptr = '\0';
        }
    }
    
    size_t len = strlen(args_buf);
    if (*pos >= len) return 0;
    
    size_t remaining_bytes = len - *pos;
    size_t to_copy = (remaining_bytes < count) ? remaining_bytes : count;
    
    memcpy(buf, args_buf + *pos, to_copy);
    *pos += to_copy;
    
    return to_copy;
}

static vfs_ssize_t procfs_pid_read(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos) {
    rv64_process_t* process = (rv64_process_t*)file->dev_data;
    if (process == NULL) return -1;

    char pid_str[16];
    itoa((int)process->pid, pid_str, 10);

    size_t len = strlen(pid_str);
    if (*pos >= (vfs_off_t)len) return 0;

    size_t remaining = len - *pos;
    size_t to_copy = (remaining < count) ? remaining : count;

    memcpy(buf, pid_str + *pos, to_copy);
    *pos += to_copy;

    return to_copy;
}

vfs_ssize_t procfs_cpuinfo(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos) {
    (void)file;
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
    (void)file;
    char meminfo_buf[512];

    size_t memTotal = get_memory_total();
    size_t buddyFree = get_memory_free();
    size_t allocated = get_memory_used();
    size_t memUsed = allocated;
    size_t memFree = memTotal - allocated;

    size_t overhead = 0;

    char total_str[32], used_str[32], free_str[32], ovh_str[32], buddy_free_str[32];
    format_memory_size(memTotal, total_str);
    format_memory_size(memUsed, used_str);
    format_memory_size(memFree, free_str);
    format_memory_size(overhead, ovh_str);
    format_memory_size(buddyFree, buddy_free_str);

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
    (void)file; (void)buf; (void)count; (void)pos;
    return 0;
}

vfs_ssize_t procfs_uptime(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos) {
    (void)file;
    
    uint64_t uptime_ms = timer_get_uptime();
    uint64_t seconds = uptime_ms / 1000;
    uint64_t centiseconds = (uptime_ms % 1000) / 10;
    
    char uptime_buffer[64];
    char seconds_str[24];
    char centiseconds_str[8];
    
    itoa((int)seconds, seconds_str, 10);
    itoa((int)centiseconds, centiseconds_str, 10);
    
    size_t idx = 0;
    
    for (const char* p = seconds_str; *p; p++) {
        uptime_buffer[idx++] = *p;
    }
    uptime_buffer[idx++] = '.';
    
    if (centiseconds < 10) {
        uptime_buffer[idx++] = '0';
    }

    for (const char* p = centiseconds_str; *p; p++) {
        uptime_buffer[idx++] = *p;
    }
    
    uptime_buffer[idx++] = '\n';
    uptime_buffer[idx] = '\0';
    
    size_t len = idx;
    if (*pos >= (vfs_off_t)len) {
        return 0;
    }

    size_t remaining = len - *pos;
    size_t to_copy = remaining < count ? remaining : count;
    
    memcpy(buf, uptime_buffer + *pos, to_copy);
    *pos += to_copy;
    
    return (vfs_ssize_t)to_copy;
}

vfs_ssize_t procfs_version(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos) {
    (void)file;
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


void procfs_set_args(int pid, char* argv[], int argc) {
    for (int i = 0; i < RV64_MAX_PROCESSES; i++) {
        if (procfs_args[i].used && procfs_args[i].pid == pid) {
            for (int j = 0; j < procfs_args[i].argc; j++) {
                if (procfs_args[i].args[j]) {
                    kfree(procfs_args[i].args[j]);
                }
            }
            procfs_args[i].used = false;
            break;
        }
    }
    
    for (int i = 0; i < RV64_MAX_PROCESSES; i++) {
        if (!procfs_args[i].used) {
            procfs_args[i].pid = pid;
            procfs_args[i].argc = argc;
            for (int j = 0; j < argc && j < MAX_ARGS_PER_PROCESS; j++) {
                int len = strlen(argv[j]);
                procfs_args[i].args[j] = kmalloc(len + 1);
                if (procfs_args[i].args[j]) {
                    strcpy(procfs_args[i].args[j], argv[j]);
                }
            }
            procfs_args[i].used = true;
            break;
        }
    }
}

void procfs_clear_args(int pid) {
    for (int i = 0; i < RV64_MAX_PROCESSES; i++) {
        if (procfs_args[i].used && procfs_args[i].pid == pid) {
            for (int j = 0; j < procfs_args[i].argc; j++) {
                if (procfs_args[i].args[j]) {
                    kfree(procfs_args[i].args[j]);
                }
            }
            procfs_args[i].used = false;
            break;
        }
    }
}

void procfs_register(int pid, void* process_data) {
    char pid_str[16];
    char relpath[128];

    itoa(pid, pid_str, 10);

    procfs_add_entry(pid_str, NULL, process_data, true);

    strcpy(relpath, pid_str);
    strcat(relpath, "/status");
    procfs_add_entry(relpath, procfs_status_read, process_data, false);

    strcpy(relpath, pid_str);
    strcat(relpath, "/registers");
    procfs_add_entry(relpath, procfs_registers_read, process_data, false);

    strcpy(relpath, pid_str);
    strcat(relpath, "/args");
    procfs_add_entry(relpath, procfs_args_read, process_data, false);

    strcpy(relpath, pid_str);
    strcat(relpath, "/pid");
    procfs_add_entry(relpath, procfs_pid_read, process_data, false);
}

void procfs_unregister(int pid) {
    char pid_str[16];
    char relpath[128];

    itoa(pid, pid_str, 10);

    strcpy(relpath, pid_str);
    strcat(relpath, "/status");
    procfs_remove_entry(relpath);

    strcpy(relpath, pid_str);
    strcat(relpath, "/registers");
    procfs_remove_entry(relpath);

    strcpy(relpath, pid_str);
    strcat(relpath, "/args");
    procfs_remove_entry(relpath);

    strcpy(relpath, pid_str);
    strcat(relpath, "/pid");
    procfs_remove_entry(relpath);

    procfs_remove_entry(pid_str);

    procfs_clear_args(pid);
}

void procfs_init(void) {
    for (int i = 0; i < MAX_PROCFS_ENTRIES; i++) {
        procfs_entries[i].used = false;
    }
    
    for (int i = 0; i < RV64_MAX_PROCESSES; i++) {
        procfs_args[i].used = false;
    }

    vfs_register_filesystem("procfs", &procfs_ops, VFS_FS_NODEV | VFS_FS_VIRTUAL | VFS_FS_READONLY);
    vfs_mount_fs("procfs", "/proc", NULL, VFS_MNT_READONLY, NULL);

    procfs_add_entry("cpuinfo", procfs_cpuinfo, NULL, false);
    procfs_add_entry("meminfo", procfs_meminfo, NULL, false);
    procfs_add_entry("pci", procfs_pci, NULL, false);
    procfs_add_entry("uptime", procfs_uptime, NULL, false);
    procfs_add_entry("version", procfs_version, NULL, false);

    cpuinfo_init();
}