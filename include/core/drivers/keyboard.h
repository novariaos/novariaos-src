#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

#define KEYBOARD_BUFFER_SIZE 256

// Initialize the keyboard driver
void keyboard_init(void);

// Read a character from the keyboard buffer (blocking)
char keyboard_getchar(void);

// Check if there's a character available
bool keyboard_has_char(void);

// Get a line of input from the keyboard
void keyboard_getline(char* buffer, int max_length);

#endif // _KEYBOARD_H_
