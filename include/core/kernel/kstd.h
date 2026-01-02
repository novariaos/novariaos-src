#ifndef _KSTD_H
#define _KSTD_H

#include <stdbool.h>
#include <stddef.h>

void reverse(char* str, int length);
char* itoa(int num, char* str, int base);
void strcpy_safe(char *dest, const char *src, size_t max_len);
char* strcpy(char *dest, const char *src);
char* strcat(char *dest, const char *src);
int strncmp(const char *s1, const char *s2, size_t n);
size_t strlen(const char* str);
int strcmp(const char* str1, const char* str2);
char* strchr(const char* str, int c);
char* strstr(const char* haystack, const char* needle);
void strcat_safe(char *dest, const char *src, size_t max_len);

#endif // _KSTD_H
