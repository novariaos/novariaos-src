// SPDX-License-Identifier: GPL-3.0-only

#include <core/fs/dirent.h>
#include <core/kernel/kstd.h>

int dirent_init(fs_dirent_t* dent, uint32_t ino, uint8_t type, const char* name) {
    if (!dent || !name) {
        return -1;
    }

    size_t name_len = strlen(name);
    if (name_len == 0 || name_len > MAX_DIRENT_NAME) {
        return -1;
    }

    dent->ino = ino;
    dent->type = type;
    dent->name_len = (uint16_t)name_len;
    strcpy_safe(dent->name, name, sizeof(dent->name));

    return 0;
}

int dirent_name_valid(const char* name, size_t len) {
    if (!name || len == 0 || len > MAX_DIRENT_NAME) {
        return 0;
    }

    // Check for invalid characters
    for (size_t i = 0; i < len; i++) {
        char c = name[i];

        // Null byte should only be at the end
        if (c == '\0') {
            return (i == len - 1) ? 1 : 0;
        }

        // Disallow path separators in name
        if (c == '/' || c == '\\') {
            return 0;
        }
    }

    // Special names
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 1; // These are valid
    }

    return 1;
}

int dirent_compare(const fs_dirent_t* a, const fs_dirent_t* b) {
    if (!a || !b) {
        return 0;
    }
    return strcmp(a->name, b->name);
}

int dirent_parse_name(const char* buf, size_t max_len, char* name_out, size_t name_out_size) {
    if (!buf || !name_out || name_out_size == 0) {
        return -1;
    }

    size_t len = 0;
    while (len < max_len && len < name_out_size - 1 && buf[len] != '\0') {
        name_out[len] = buf[len];
        len++;
    }
    name_out[len] = '\0';

    return (int)len;
}

size_t dirent_aligned_size(size_t name_len, size_t alignment) {
    if (alignment == 0) {
        alignment = 1;
    }

    // Base size: inode + type + name_len field
    size_t base_size = sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint16_t);
    size_t total_size = base_size + name_len + 1; // +1 for null terminator

    // Align to boundary
    size_t remainder = total_size % alignment;
    if (remainder != 0) {
        total_size += (alignment - remainder);
    }

    return total_size;
}
