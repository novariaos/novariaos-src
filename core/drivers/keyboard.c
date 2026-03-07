// SPDX-License-Identifier: GPL-3.0-only

#include <core/drivers/keyboard.h>
#include <core/kernel/kstd.h>
#include <core/kernel/vge/fb.h>
#include <core/arch/io.h>
#include <core/kernel/nvm/nvm.h>
#include <stdbool.h>

#define KEYBOARD_DATA_PORT    0x60
#define KEYBOARD_STATUS_PORT  0x64

#define HOTKEY_MOD_SHIFT 1
#define HOTKEY_MOD_CTRL  2
#define HOTKEY_MOD_ALT   4

static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static volatile int buffer_read_pos = 0;
static volatile int buffer_write_pos = 0;

static const char scancode_to_ascii[] = {
    0,   27,  '1', '2', '3',  '4', '5', '6', '7', '8', '9', '0', '-',  '=', '\b',
    '\t', 'q', 'w', 'e', 'r',  't', 'y', 'u', 'i', 'o', 'p', '[', ']',  '\n',
    0,    'a', 's', 'd', 'f',  'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',  0,
    '*',  0,   ' '
};

static const char scancode_to_ascii_shifted[] = {
    0,   27,  '!', '@', '#',  '$', '%', '^', '&', '*', '(', ')', '_',  '+', '\b',
    '\t', 'Q', 'W', 'E', 'R',  'T', 'Y', 'U', 'I', 'O', 'P', '{', '}',  '\n',
    0,    'A', 'S', 'D', 'F',  'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',  0,
    '*',  0,   ' '
};

static bool shift_pressed = false;
static bool caps_lock = false;
static bool ctrl_pressed = false;
static bool alt_pressed = false;

static struct {
    int scancode;
    int modifiers;
    void (*callback)(void*);
    void* data;
    bool used;
} hotkeys[16];

static void keyboard_buffer_push(char c) {
    int next_pos = (buffer_write_pos + 1) % KEYBOARD_BUFFER_SIZE;
    if (next_pos != buffer_read_pos) {
        keyboard_buffer[buffer_write_pos] = c;
        buffer_write_pos = next_pos;
    }
}

static char keyboard_buffer_pop(void) {
    if (buffer_read_pos == buffer_write_pos) {
        return 0;
    }
    char c = keyboard_buffer[buffer_read_pos];
    buffer_read_pos = (buffer_read_pos + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

int keyboard_register_hotkey(int scancode, int modifiers, void (*callback)(void*), void* data) {
    for (int i = 0; i < 16; i++) {
        if (!hotkeys[i].used) {
            hotkeys[i].scancode = scancode;
            hotkeys[i].modifiers = modifiers;
            hotkeys[i].callback = callback;
            hotkeys[i].data = data;
            hotkeys[i].used = true;
            return i;
        }
    }
    return -1;
}

void keyboard_unregister_hotkey(int id) {
    if (id >= 0 && id < 16) {
        hotkeys[id].used = false;
    }
}

static void check_hotkeys(int scancode) {
    int mods = 0;
    if (shift_pressed) mods |= HOTKEY_MOD_SHIFT;
    if (ctrl_pressed) mods |= HOTKEY_MOD_CTRL;
    if (alt_pressed) mods |= HOTKEY_MOD_ALT;
    
    for (int i = 0; i < 16; i++) {
        if (hotkeys[i].used && 
            hotkeys[i].scancode == scancode && 
            hotkeys[i].modifiers == mods) {
            if (hotkeys[i].callback) {
                hotkeys[i].callback(hotkeys[i].data);
            }
            return;
        }
    }
}

void keyboard_handler(void) {
    int8_t scancode = inb(KEYBOARD_DATA_PORT);

    if (scancode & 0x80) {
        scancode &= 0x7F;

        if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = false;
        }

        if (scancode == 0x1D) {
            ctrl_pressed = false;
        }

        if (scancode == 0x38) {
            alt_pressed = false;
        }

        return;
    }

    check_hotkeys(scancode);

    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = true;
        return;
    }

    if (scancode == 0x1D) {
        ctrl_pressed = true;
        return;
    }

    if (scancode == 0x38) {
        alt_pressed = true;
        return;
    }

    if (scancode == 0x3A) {
        caps_lock = !caps_lock;
        return;
    }

    char ascii = 0;
    if (scancode < sizeof(scancode_to_ascii)) {
        if (shift_pressed) {
            ascii = scancode_to_ascii_shifted[scancode];
        } else {
            ascii = scancode_to_ascii[scancode];

            if (caps_lock && ascii >= 'a' && ascii <= 'z') {
                ascii = ascii - 'a' + 'A';
            }
        }

        if (ctrl_pressed && ascii >= 'a' && ascii <= 'z') {
            ascii = ascii - 'a' + 1;
        } else if (ctrl_pressed && ascii >= 'A' && ascii <= 'Z') {
            ascii = ascii - 'A' + 1;
        }

        if (ascii != 0) {
            keyboard_buffer_push(ascii);
        }
    }
}

static void keyboard_poll(void) {
    int8_t status = inb(KEYBOARD_STATUS_PORT);
    if (status & 0x01) {
        keyboard_handler();
    }
}

void keyboard_init(void) {
    buffer_read_pos = 0;
    buffer_write_pos = 0;
    shift_pressed = false;
    caps_lock = false;
    ctrl_pressed = false;
    alt_pressed = false;
    
    for (int i = 0; i < 16; i++) {
        hotkeys[i].used = false;
    }
}

bool keyboard_has_char(void) {
    keyboard_poll();
    return buffer_read_pos != buffer_write_pos;
}

char keyboard_getchar(void) {    
    while (!keyboard_has_char()) {
        keyboard_poll();
        nvm_scheduler_tick();
    }
    return keyboard_buffer_pop();
}

void keyboard_getline(char* buffer, int max_length) {
    int pos = 0;

    while (pos < max_length - 1) {
        char c = keyboard_getchar();

        if (c == '\n') {
            buffer[pos] = '\0';
            kprint("\n", 7);
            return;
        } else if (c == '\b') {
            if (pos > 0) {
                pos--;
                vga_backspace();
            }
            continue;
        } else if (c >= 32 && c <= 126) {
            buffer[pos++] = c;
            char temp[2] = {c, '\0'};
            kprint(temp, 15);
        }
    }

    buffer[pos] = '\0';
}