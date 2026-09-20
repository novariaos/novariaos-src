#ifndef NVLIBC_H
#define NVLIBC_H

typedef unsigned long size_t;
typedef long ssize_t;

#define NULL ((void *)0)

void exit(int code);
long nv_write(const void *buf, size_t len);
int putc(char c);
int puts(const char *s);

size_t nv_strlen(const char *s);
int nv_strcmp(const char *a, const char *b);
void *nv_memset(void *dst, int c, size_t n);
void *nv_memcpy(void *dst, const void *src, size_t n);
void *nv_memmove(void *dst, const void *src, size_t n);

void nv_exit(int code);
long nv_tty_write(const void *buf, size_t len);

#endif