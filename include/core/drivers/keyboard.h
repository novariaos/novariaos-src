#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

#define KEYBOARD_BUFFER_SIZE 256
#define HOTKEY_MOD_SHIFT 1
#define HOTKEY_MOD_CTRL  2
#define HOTKEY_MOD_ALT   4

int keyboard_register_hotkey(int scancode, int modifiers, void (*callback)(void*), void* data);
void keyboard_unregister_hotkey(int id);
void keyboard_init(void);
char keyboard_getchar(void);
bool keyboard_has_char(void);
void keyboard_getline(char* buffer, int max_length);

#endif // _KEYBOARD_H_
