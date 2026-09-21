#ifndef NVLIBC_H
#define NVLIBC_H

typedef unsigned long size_t;
typedef long ssize_t;

#define NULL ((void *)0)

void exit(int code);
int spawn(char* bin_path);
void sleep(int milliseconds);
long write(const void *buf, size_t len);
int putchar(char c);
int puts(const char *s);

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);

void nv_exit(int code);
int nv_spawn(char* bin_path);
void nv_sleep(int milliseconds);
long nv_tty_write(const void *buf, size_t len);

#endif