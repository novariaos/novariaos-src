#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

#define KEYBOARD_BUFFER_SIZE 256

void keyboard_init(void);
char keyboard_getchar(void);
bool keyboard_has_char(void);
void keyboard_getline(char* buffer, int max_length);

#endif // _KEYBOARD_H_
