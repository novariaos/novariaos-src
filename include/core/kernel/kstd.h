#ifndef _KSTD_H
#define _KSTD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
void* memmove(void *dest, const void *src, size_t n);
void kprint(const char *str, int color);
void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* s, int c, size_t n);

// Endianness conversion utilities
// x86_64 is little-endian, so these are mostly no-ops
// but provide portable API for filesystem implementations

// Little-endian conversions (for FAT32, EXT2, etc.)
static inline uint16_t le16_to_cpu(uint16_t val) {
    return val;  // x86_64 is little-endian
}

static inline uint32_t le32_to_cpu(uint32_t val) {
    return val;
}

static inline uint64_t le64_to_cpu(uint64_t val) {
    return val;
}

static inline uint16_t cpu_to_le16(uint16_t val) {
    return val;
}

static inline uint32_t cpu_to_le32(uint32_t val) {
    return val;
}

static inline uint64_t cpu_to_le64(uint64_t val) {
    return val;
}

// Big-endian conversions (for ISO9660, network protocols, etc.)
static inline uint16_t be16_to_cpu(uint16_t val) {
    return ((val & 0xFF00) >> 8) | ((val & 0x00FF) << 8);
}

static inline uint32_t be32_to_cpu(uint32_t val) {
    return ((val & 0xFF000000) >> 24) |
           ((val & 0x00FF0000) >> 8)  |
           ((val & 0x0000FF00) << 8)  |
           ((val & 0x000000FF) << 24);
}

static inline uint64_t be64_to_cpu(uint64_t val) {
    return ((val & 0xFF00000000000000ULL) >> 56) |
           ((val & 0x00FF000000000000ULL) >> 40) |
           ((val & 0x0000FF0000000000ULL) >> 24) |
           ((val & 0x000000FF00000000ULL) >> 8)  |
           ((val & 0x00000000FF000000ULL) << 8)  |
           ((val & 0x0000000000FF0000ULL) << 24) |
           ((val & 0x000000000000FF00ULL) << 40) |
           ((val & 0x00000000000000FFULL) << 56);
}

static inline uint16_t cpu_to_be16(uint16_t val) {
    return be16_to_cpu(val);  // Symmetric operation
}

static inline uint32_t cpu_to_be32(uint32_t val) {
    return be32_to_cpu(val);
}

static inline uint64_t cpu_to_be64(uint64_t val) {
    return be64_to_cpu(val);
}

#endif // _KSTD_H
