#ifndef SERIAL_H
#define SERIAL_H

#include <core/kernel/kstd.h>

#define PORT 0x3f8 // COM1

char read_serial();
int init_serial();
int serial_received();
int is_transmit_empty();
void write_serial(char a);
void serial_print(const char* str);

#endif // SERIAL_H