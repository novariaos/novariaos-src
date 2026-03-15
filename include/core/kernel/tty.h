#ifndef CORE_TTY_H
#define CORE_TTY_H

#include <types.h>

typedef struct {
    void (*puts)(const char* str, int default_color);
    void (*clear)(void);
} tty_driver_t;

void tty_register_driver(tty_driver_t* driver);
void tty_puts(const char* str);
void tty_clear(void);

static tty_driver_t* current_driver = NULL;
static int escape_state = 0;
static int current_param = 0;
static int params[16];
static int param_count = 0;
static int current_fg = 7;
static int current_bg = 0;

#endif