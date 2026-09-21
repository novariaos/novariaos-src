#include <nvlibc.h>

long write(const void *buf, size_t len) {
    return nv_tty_write(buf, len);
}

int putchar(char c) {
    char byte = c;
    if (write(&byte, 1) != 1) {
        return -1;
    }
    return 0;
}

int puts(const char *s) {
    size_t len = strlen(s);
    if (write(s, len) != (long)len) {
        return -1;
    }
    return (int)len;
}

void sleep(int milliseconds) {
    nv_sleep(milliseconds);
}

int spawn(char* bin_path) {
    return nv_spawn(bin_path);
}