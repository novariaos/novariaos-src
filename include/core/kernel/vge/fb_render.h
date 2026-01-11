#ifndef _FB_RENDER_H_
#define _FB_RENDER_H_

#include <stdint.h>

// Built-in fallback font (used when system_font is not loaded)
extern const uint8_t builtin_font[128][8];

// Framebuffer rendering functions
void init_fb(void);
void clear_screen(void);
void newline(void);
void fb_putchar(char c, int color);
void vgaprint(const char *str, int color);
void set_bg_color(uint32_t color);
void set_fg_color(uint32_t color);
void vga_backspace(void);
void set_cursor_pos(uint32_t x, uint32_t y);
uint32_t get_screen_width_chars(void);
uint32_t get_screen_height_chars(void);
void draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
void draw_line(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t color);

#endif // _FB_RENDER_H_
