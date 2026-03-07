#ifndef _FB_H_
#define _FB_H_

#include <stdint.h>
#include <core/kernel/vge/fb_render.h>

void disable_cursor();
void enable_cursor();
void clear_screen(void);
void newline(void);
void vgaprint(const char *str, int color);
void terminal_set_cursor(int x, int y);
void vga_backspace(void);
void vga_scroll_up(void);
void vga_scroll_down(void);
int init_vge_font(void);

#endif
