#include <nvlibc.h>

long nv_write(const void *buf, size_t len) {
    return nv_tty_write(buf, len);
}

int putc(char c) {
    char byte = c;
    if (nv_write(&byte, 1) != 1) {
        return -1;
    }
    return 0;
}

int puts(const char *s) {
    size_t len = nv_strlen(s);
    if (nv_write(s, len) != (long)len) {
        return -1;
    }
    return (int)len;
}