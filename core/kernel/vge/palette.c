// SPDX-License-Identifier: GPL-3.0-only

#include <core/fs/vfs.h>
#include <core/kernel/kstd.h>
#include <core/kernel/vge/palette.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

static uint32_t vga_palette[16] = {
    0x00101010, // 0  black
    0x003b5bdb, // 1  blue
    0x0031a354, // 2  green
    0x0030a0a0, // 3  cyan
    0x00c34043, // 4  red
    0x007b3fb2, // 5  magenta
    0x00b58900, // 6  yellow
    0x00c0c0c0, // 7  white
    0x00505050, // 8  bright black
    0x006a8cff, // 9  bright blue
    0x0057d18b, // 10 bright green
    0x005fd7d7, // 11 bright cyan
    0x00ff6b6b, // 12 bright red
    0x00c77dff, // 13 bright magenta
    0x00ffd866, // 14 bright yellow
    0x00f2f2f2, // 15 bright white
};

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_hex_color(const char* s, uint32_t* out) {
    if (*s != '#')
        return -1;
    s++;

    uint32_t color = 0;
    for (int i = 0; i < 6; i++) {
        int d = hex_digit(s[i]);
        if (d < 0)
            return -1;
        color = (color << 4) | (uint32_t)d;
    }

    *out = color;
    return 0;
}

static int color_name_to_offset(const char* name, size_t len) {
    if (len == 5 && strncmp(name, "black", 5) == 0)   return 0;
    if (len == 4 && strncmp(name, "blue", 4) == 0)     return 1;
    if (len == 5 && strncmp(name, "green", 5) == 0)    return 2;
    if (len == 4 && strncmp(name, "cyan", 4) == 0)     return 3;
    if (len == 3 && strncmp(name, "red", 3) == 0)      return 4;
    if (len == 7 && strncmp(name, "magenta", 7) == 0)  return 5;
    if (len == 6 && strncmp(name, "yellow", 6) == 0)   return 6;
    if (len == 5 && strncmp(name, "white", 5) == 0)    return 7;
    return -1;
}

static void parse_palette_conf(const char* data, size_t size) {
    int section_base = -1;
    size_t pos = 0;

    while (pos < size) {
        size_t line_start = pos;
        while (pos < size && data[pos] != '\n')
            pos++;
        size_t line_end = pos;
        if (pos < size)
            pos++;

        size_t i = line_start;
        while (i < line_end && (data[i] == ' ' || data[i] == '\t'))
            i++;

        if (i >= line_end || data[i] == '#')
            continue;

        if (data[i] == '[') {
            i++;
            size_t sec_start = i;
            while (i < line_end && data[i] != ']')
                i++;
            size_t sec_len = i - sec_start;

            if (sec_len == 14 && strncmp(data + sec_start, "palette.normal", 14) == 0)
                section_base = 0;
            else if (sec_len == 14 && strncmp(data + sec_start, "palette.bright", 14) == 0)
                section_base = 8;
            else
                section_base = -1;
            continue;
        }

        if (section_base < 0)
            continue;

        size_t name_start = i;
        while (i < line_end && data[i] != ' ' && data[i] != '\t' && data[i] != '=')
            i++;
        size_t name_len = i - name_start;

        int offset = color_name_to_offset(data + name_start, name_len);
        if (offset < 0)
            continue;

        while (i < line_end && (data[i] == ' ' || data[i] == '\t'))
            i++;
        if (i >= line_end || data[i] != '=')
            continue;
        i++;
        while (i < line_end && (data[i] == ' ' || data[i] == '\t'))
            i++;

        uint32_t color;
        if (parse_hex_color(data + i, &color) == 0)
            vga_palette[section_base + offset] = color;
    }
}

void palette_init(void) {
    if (!vfs_exists(PALETTE_PATH))
        return;

    size_t file_size = 0;
    const char* data = vfs_read(PALETTE_PATH, &file_size);
    if (!data || file_size == 0)
        return;

    kprint("Loading palette: ", 7);
    kprint(PALETTE_PATH, 7);
    kprint("\n", 7);

    parse_palette_conf(data, file_size);
}

uint32_t palette_get_color(int index) {
    return vga_palette[index & 0xF];
}
