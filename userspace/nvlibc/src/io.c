#include <nvlibc.h>

long read(int fd, void *buf, size_t len) {
    return nv_read(fd, buf, len);
}

long write(int fd, const void *buf, size_t len) {
    return nv_write(fd, buf, len);
}

int putchar(char c) {
    char byte = c;
    if (write(1, &byte, 1) != 1) {
        return -1;
    }
    return 0;
}

int puts(const char *s) {
    size_t len = strlen(s);
    if (write(1, s, len) != (long)len) {
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

int open(char* path) {
    return nv_open(path);
}

void close(int fd) {
    nv_close(fd);
}

int mkdir(const char* path) {
    int result = nv_mkdir((char*)path);
    return result < 0 ? result : 0;
}