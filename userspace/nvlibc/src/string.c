#include <nvlibc.h>
#include <stdarg.h>

static void out_char(char *buf, size_t cap, size_t *pos, char c) {
    if (*pos + 1 < cap) {
        buf[*pos] = c;
    }
    *pos = *pos + 1;
}

static void out_pad(char *buf, size_t cap, size_t *pos, char c, int n) {
    while (n > 0) {
        out_char(buf, cap, pos, c);
        n = n - 1;
    }
}

static int to_unsigned(unsigned long v, unsigned base, int upper, char *tmp) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    if (v == 0) {
        tmp[n] = '0';
        n = n + 1;
    } else {
        while (v != 0) {
            tmp[n] = digits[v % base];
            n = n + 1;
            v = v / base;
        }
    }
    return n;
}

static void out_rev(char *buf, size_t cap, size_t *pos, const char *tmp, int n) {
    while (n > 0) {
        n = n - 1;
        out_char(buf, cap, pos, tmp[n]);
    }
}

int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap) {
    size_t pos = 0;

    if (cap == 0) {
        buf = 0;
    }

    while (*fmt != '\0') {
        if (*fmt != '%') {
            out_char(buf, cap, &pos, *fmt);
            fmt = fmt + 1;
            continue;
        }

        fmt = fmt + 1;

        int left = 0;
        int zero = 0;
        int plus = 0;
        int space = 0;
        int width = 0;
        int prec = -1;
        int is_long = 0;

        for (;;) {
            if (*fmt == '-') { left = 1; fmt = fmt + 1; continue; }
            if (*fmt == '0') { zero = 1; fmt = fmt + 1; continue; }
            if (*fmt == '+') { plus = 1; fmt = fmt + 1; continue; }
            if (*fmt == ' ') { space = 1; fmt = fmt + 1; continue; }
            break;
        }

        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) { left = 1; width = -width; }
            fmt = fmt + 1;
        } else {
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt = fmt + 1;
            }
        }

        if (*fmt == '.') {
            fmt = fmt + 1;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                if (prec < 0) prec = -1;
                fmt = fmt + 1;
            } else {
                prec = 0;
                while (*fmt >= '0' && *fmt <= '9') {
                    prec = prec * 10 + (*fmt - '0');
                    fmt = fmt + 1;
                }
            }
        }

        if (*fmt == 'l') { is_long = 1; fmt = fmt + 1; }

        char c = *fmt;
        fmt = fmt + 1;

        char tmp[32];
        int n = 0;
        int neg = 0;
        int base = 10;
        int upper = 0;
        int is_signed = 0;
        unsigned long uv = 0;
        const char *s = 0;

        if (c == 'd' || c == 'i') {
            long v;
            if (is_long) v = va_arg(ap, long);
            else v = va_arg(ap, int);
            if (v < 0) { neg = 1; uv = (unsigned long)(-(v + 1)) + 1UL; }
            else { uv = (unsigned long)v; }
            is_signed = 1;
        } else if (c == 'u') {
            if (is_long) uv = va_arg(ap, unsigned long);
            else uv = va_arg(ap, unsigned int);
        } else if (c == 'x' || c == 'X') {
            base = 16;
            upper = (c == 'X');
            if (is_long) uv = va_arg(ap, unsigned long);
            else uv = va_arg(ap, unsigned int);
        } else if (c == 'o') {
            base = 8;
            if (is_long) uv = va_arg(ap, unsigned long);
            else uv = va_arg(ap, unsigned int);
        } else if (c == 'p') {
            base = 16;
            uv = (unsigned long)(size_t)va_arg(ap, void *);
        } else if (c == 'c') {
            tmp[0] = (char)va_arg(ap, int);
            n = 1;
            is_signed = -1;
        } else if (c == 's') {
            s = va_arg(ap, const char *);
            if (s == 0) s = "(null)";
            is_signed = -1;
        } else if (c == '%') {
            tmp[0] = '%';
            n = 1;
            is_signed = -1;
        } else {
            tmp[0] = '%';
            tmp[1] = c;
            n = 2;
            is_signed = -1;
        }

        if (is_signed >= 0) {
            n = to_unsigned(uv, (unsigned)base, upper, tmp);
        }

        int slen = 0;
        if (c == 's') {
            while (s[slen] != '\0' && (prec < 0 || slen < prec)) slen = slen + 1;
        } else if (c == 'p') {
            slen = -1;
        } else if (is_signed == -1) {
            slen = n;
        } else {
            slen = n;
            if (prec >= 0 && prec > n) slen = prec;
            if (is_signed == 1) {
                if (neg) slen = slen + 1;
                else if (plus || space) slen = slen + 1;
            }
        }

        int pad = 0;
        char padc = ' ';
        if (c == 'p') {
            pad = 0;
            if (width > 2 + n) pad = width - 2 - n;
            if (!left) out_pad(buf, cap, &pos, ' ', pad);
            out_char(buf, cap, &pos, '0');
            out_char(buf, cap, &pos, 'x');
            out_rev(buf, cap, &pos, tmp, n);
            if (left) out_pad(buf, cap, &pos, ' ', pad);
            continue;
        }

        if (width > slen) pad = width - slen;
        if (zero && !left && is_signed != -1) padc = '0';

        if (!left) {
            if (padc == '0' && is_signed == 1) {
                if (neg) out_char(buf, cap, &pos, '-');
                else if (plus) out_char(buf, cap, &pos, '+');
                else if (space) out_char(buf, cap, &pos, ' ');
            }
            out_pad(buf, cap, &pos, padc, pad);
        }

        if (is_signed == -1) {
            if (c == 's') {
                int i = 0;
                while (i < slen) { out_char(buf, cap, &pos, s[i]); i = i + 1; }
            } else {
                int i = 0;
                while (i < n) { out_char(buf, cap, &pos, tmp[i]); i = i + 1; }
            }
        } else {
            if (!(padc == '0' && !left)) {
                if (neg) out_char(buf, cap, &pos, '-');
                else if (plus) out_char(buf, cap, &pos, '+');
                else if (space) out_char(buf, cap, &pos, ' ');
            }
            if (prec >= 0) out_pad(buf, cap, &pos, '0', prec - n);
            out_rev(buf, cap, &pos, tmp, n);
        }

        if (left) out_pad(buf, cap, &pos, ' ', pad);
    }

    if (cap > 0) {
        if (pos < cap) buf[pos] = '\0';
        else buf[cap - 1] = '\0';
    }

    return (int)pos;
}

int snprintf(char *buf, size_t cap, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, (size_t)-1, fmt, ap);
    va_end(ap);
    return n;
}

int printf(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    size_t len = (n < (int)sizeof buf) ? (size_t)n : sizeof buf - 1;
    if (write(1, buf, len) != (long)len) return -1;
    return n;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len] != '\0') {
        len = len + 1;
    }
    return len;
}

int strcmp(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        a = a + 1;
        b = b + 1;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}