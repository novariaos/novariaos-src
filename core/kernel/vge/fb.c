#include <stdint.h>
#include <lib/bootloader/limine.h>
#include <stddef.h>
#include <core/kernel/mem.h>
#include <core/kernel/kstd.h>
#include <core/kernel/vge/fb_render.h>

#define FONT_HEIGHT 16

// External declarations
extern int load_font_from_vfs(const char* path, uint8_t font[256][FONT_HEIGHT]);
extern bool vfs_exists(const char* filename);

int system_font_height = 16;
uint8_t system_font[256][FONT_HEIGHT];
int init_vge_font() {
    const char* font_path = "/usr/share/fonts/general.psf";

    memset(system_font, 0, sizeof(system_font));
    system_font_height = 16;

    if (!vfs_exists(font_path)) {
        kprint(":: Font file not found, using built-in 8x8 font.\n", 14);
        system_font_height = 8;
        for (int i = 0; i < 256; i++) {
            for (int row = 0; row < 8; row++) {
                system_font[i][row] = builtin_font[i][row];
            }
        }
        return -1;
    }

    int result = load_font_from_vfs(font_path, system_font);
    if (result == 0) {
        kprint(":: External font loaded (16px height).\n", 2);
        system_font_height = 16;
        return 0;
    }

    kprint(":: Font loading failed, using built-in 8x8 font.\n", 14);
    system_font_height = 8;
    for (int i = 0; i < 256; i++) {
        for (int row = 0; row < 8; row++) {
            system_font[i][row] = builtin_font[i][row];
        }
    }
    return -1;
}