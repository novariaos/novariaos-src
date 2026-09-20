#include <nvlibc.h>

size_t nv_strlen(const char *s) {
    size_t len = 0;
    while (s[len] != '\0') {
        len = len + 1;
    }
    return len;
}

int nv_strcmp(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        a = a + 1;
        b = b + 1;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}