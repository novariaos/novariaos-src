// SPDX-License-Identifier: GPL-3.0-only

#include <core/kernel/kstd.h>
#include <stdint.h>

void reverse(char* str, int length) {
    int start = 0;
    int end = length - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

char* strcpy(char *dest, const char *src) {
    char *d = dest;
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
    return d;
}

void strcpy_safe(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    while (i < max_len - 1 && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

char* strcat(char *dest, const char *src) {
    char *d = dest;
    while (*dest) {
        dest++;
    }
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
    return d;
}

void strcat_safe(char *dest, const char *src, size_t max_len) {
    size_t dest_len = strlen(dest);
    size_t i = 0;
    while (dest_len + i < max_len - 1 && src[i] != '\0') {
        dest[dest_len + i] = src[i];
        i++;
    }
    dest[dest_len + i] = '\0';
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n > 0 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

char* itoa(int num, char* str, int base) {
    int i = 0;
    bool is_negative = false;

    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return str;
    }

    if (num < 0 && base == 10) {
        is_negative = true;
        num = -num;
    }

    while (num != 0) {
        int rem = num % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num /= base;
    }

    if (is_negative) {
        str[i++] = '-';
    }

    str[i] = '\0';

    reverse(str, i);

    return str;
}

size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

int strcmp(const char* str1, const char* str2) {
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return *(unsigned char*)str1 - *(unsigned char*)str2;
}

char* strchr(const char* str, int c) {
    while (*str != '\0') {
        if (*str == (char)c) {
            return (char*)str;
        }
        str++;
    }
    return NULL;
}

char* strstr(const char* haystack, const char* needle) {
    if (*needle == '\0') {
        return (char*)haystack;
    }

    for (; *haystack != '\0'; haystack++) {
        const char* h = haystack;
        const char* n = needle;

        while (*h != '\0' && *n != '\0' && *h == *n) {
            h++;
            n++;
        }

        if (*n == '\0') {
            return (char*)haystack;
        }
    }

    return NULL;
}

void memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    
    if (d == s || n == 0) {
        return dest;
    }

    if (d < s || d >= s + n) {
        if (n >= 8) {
            uint64_t *d64 = (uint64_t *)d;
            const uint64_t *s64 = (const uint64_t *)s;
            size_t n64 = n / 8;

            for (size_t i = 0; i < n64; i++) {
                d64[i] = s64[i];
            }
            
            d += n64 * 8;
            s += n64 * 8;
            n %= 8;
        }

        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } 
    else {
        d += n;
        s += n;

        if (n >= 8) {
            uint64_t *d64 = (uint64_t *)(d - 8);
            const uint64_t *s64 = (const uint64_t *)(s - 8);
            size_t n64 = n / 8;
            
            for (size_t i = 0; i < n64; i++) {
                d64[-i] = s64[-i];
            }
            
            n %= 8;
        }

        for (size_t i = 0; i < n; i++) {
            d[-i - 1] = s[-i - 1];
        }
    }
    
    return dest;
}
