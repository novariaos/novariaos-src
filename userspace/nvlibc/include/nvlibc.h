#ifndef NVLIBC_H
#define NVLIBC_H

#include <stdarg.h>

typedef unsigned long size_t;
typedef long ssize_t;

#define NULL ((void *)0)

int  open(char *path);
void close(int fd);
long write(const void *buf, size_t len);
int  putchar(char c);
int  puts(const char *s);
void sleep(int milliseconds);
int  spawn(char *bin_path);

int  printf(const char *fmt, ...);
int  sprintf(char *buf, const char *fmt, ...);
int  snprintf(char *buf, size_t cap, const char *fmt, ...);
int  vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap);

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);

void nv_exit(int code);
int nv_spawn(char* bin_path);
void nv_sleep(int milliseconds);
int nv_open(char* path);
void nv_close(int fd);
long nv_tty_write(const void *buf, size_t len);

#endif