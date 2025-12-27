// SPDX-License-Identifier: LGPL-3.0-or-later

#include <core/fs/procfs.h>
#include <core/fs/vfs.h>
#include <core/kernel/vge/fb_render.h>
#include <core/arch/cpuid.h>
#include <core/kernel/kstd.h>
#include <core/kernel/mem.h>
#include <stdint.h>
#include <string.h>

void procfs_init() {
    vfs_pseudo_register("/proc/cpuinfo", procfs_cpuinfo, NULL, NULL, NULL, NULL);
    vfs_pseudo_register("/proc/meminfo", procfs_meminfo, NULL, NULL, NULL, NULL);
    vfs_pseudo_register("/proc/pci", procfs_pci, NULL, NULL, NULL, NULL);
    vfs_pseudo_register("/proc/uptime", procfs_uptime, NULL, NULL, NULL, NULL);
    cpuinfo_init();
}

static char cpuinfo_buf[2048];
static int cpuinfo_initialized = 0;

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
    static char meminfo_buf[512];
    static int meminfo_initialized = 0;

    if (!meminfo_initialized) {
        size_t memTotal = getMemTotal();
        size_t memFree = getMemFree();
        size_t memUsed = memTotal - memFree;

        char total_str[32], used_str[32], free_str[32];
        formatMemorySize(memTotal, total_str);
        formatMemorySize(memUsed, used_str);
        formatMemorySize(memFree, free_str);

        strcpy_safe(meminfo_buf, "MemTotal       : ", sizeof(meminfo_buf));
        strcat_safe(meminfo_buf, total_str, sizeof(meminfo_buf));
        strcat_safe(meminfo_buf, "\nMemUsed        : ", sizeof(meminfo_buf));
        strcat_safe(meminfo_buf, used_str, sizeof(meminfo_buf));
        strcat_safe(meminfo_buf, "\nMemFree        : ", sizeof(meminfo_buf));
        strcat_safe(meminfo_buf, free_str, sizeof(meminfo_buf));
        strcat_safe(meminfo_buf, "\n", sizeof(meminfo_buf));

        meminfo_initialized = 1;
    }

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

int parse_frequency_mhz(const char* str) {
    int integer_part = 0;
    int fractional_part = 0;
    int fractional_digits = 0;
    int in_fraction = 0;

    // Skip whitespace
    while (*str == ' ' || *str == '\t') {
        str++;
    }

    // Parse integer part
    while (*str >= '0' && *str <= '9') {
        integer_part = integer_part * 10 + (*str - '0');
        str++;
    }

    // Parse fractional part
    if (*str == '.') {
        str++;
        in_fraction = 1;
        while (*str >= '0' && *str <= '9') {
            fractional_part = fractional_part * 10 + (*str - '0');
            fractional_digits++;
            str++;
        }
    }

    // Convert to MHz (multiply GHz by 1000)
    int mhz = integer_part * 1000;

    // Handle fractional part - convert to MHz
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

    // Get vendor ID
    cpuid(0, 0, &result);
    char vendor[13];
    memcpy(vendor, &result.ebx, 4);
    memcpy(vendor + 4, &result.edx, 4);
    memcpy(vendor + 8, &result.ecx, 4);
    vendor[12] = '\0';

    // Vendor ID
    strcpy_safe(buf, "vendor_id       : ", remaining);
    strcat_safe(buf, vendor, remaining);
    strcat_safe(buf, "\n", remaining);
    buf += strlen(buf);
    remaining = sizeof(cpuinfo_buf) - (buf - cpuinfo_buf);

    // Get processor info for family and model
    cpuid(1, 0, &result);

    // CPU family
    strcpy_safe(buf, "cpu family      : ", remaining);
    itoa((result.eax >> 8) & 0xF, num_str, 10);
    strcat_safe(buf, num_str, remaining);
    strcat_safe(buf, "\n", remaining);
    buf += strlen(buf);
    remaining = sizeof(cpuinfo_buf) - (buf - cpuinfo_buf);

    // Model
    strcpy_safe(buf, "model           : ", remaining);
    uint8_t model = (result.eax >> 4) & 0xF;
    uint8_t extended_model = (result.eax >> 16) & 0xF;
    itoa((extended_model << 4) | model, num_str, 10);
    strcat_safe(buf, num_str, remaining);
    strcat_safe(buf, "\n", remaining);
    buf += strlen(buf);
    remaining = sizeof(cpuinfo_buf) - (buf - cpuinfo_buf);

    // Model name
    strcpy_safe(buf, "model name      : ", remaining);

    cpuid(0x80000000, 0, &result);
    if (result.eax >= 0x80000004) {
        // Leaf 0x80000002
        cpuid(0x80000002, 0, &result);
        memcpy(brand_str, &result.eax, 4);
        memcpy(brand_str + 4, &result.ebx, 4);
        memcpy(brand_str + 8, &result.ecx, 4);
        memcpy(brand_str + 12, &result.edx, 4);
        
        // Leaf 0x80000003
        cpuid(0x80000003, 0, &result);
        memcpy(brand_str + 16, &result.eax, 4);
        memcpy(brand_str + 20, &result.ebx, 4);
        memcpy(brand_str + 24, &result.ecx, 4);
        memcpy(brand_str + 28, &result.edx, 4);
        
        // Leaf 0x80000004
        cpuid(0x80000004, 0, &result);
        memcpy(brand_str + 32, &result.eax, 4);
        memcpy(brand_str + 36, &result.ebx, 4);
        memcpy(brand_str + 40, &result.ecx, 4);
        memcpy(brand_str + 44, &result.edx, 4);
        brand_str[48] = '\0';

        // Process model name
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
            
            // Try to extract frequency from model name
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

    // Stepping
    cpuid(1, 0, &result);
    strcpy_safe(buf, "stepping        : ", remaining);
    itoa(result.eax & 0xF, num_str, 10);
    strcat_safe(buf, num_str, remaining);
    strcat_safe(buf, "\n", remaining);
    buf += strlen(buf);
    remaining = sizeof(cpuinfo_buf) - (buf - cpuinfo_buf);

    // CPU MHz
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
    else {
        cpuid(0x80000007, 0, &result);
        if (result.edx & (1 << 8)) {
            strcat_safe(buf, "TSC invariant", remaining);
        } else {
            strcat_safe(buf, "measuring...", remaining);
        }
    }
    strcat_safe(buf, "\n", remaining);
    buf += strlen(buf);
    remaining = sizeof(cpuinfo_buf) - (buf - cpuinfo_buf);

    // FPU (Floating Point Unit) - need to call cpuid(1) again
    cpuid(1, 0, &result);
    strcpy_safe(buf, "fpu             : ", remaining);
    strcat_safe(buf, (result.edx & (1 << 0)) ? "yes" : "no", remaining);
    strcat_safe(buf, "\n", remaining);
}
