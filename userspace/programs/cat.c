#include <nvlibc.h>

int main() {
    int fd = open("/proc/self/args");
    if (fd < 0) {
        printf("cat: open /proc/self/args failed: %d\n", -fd);
        return 1;
    }

    char buf[1024];
    long n = read(fd, buf, sizeof buf);
    close(fd);
    if (n < 0) {
        printf("cat: read args failed: %d\n", (int)(-n));
        return 1;
    }
    buf[n] = '\0';

    char arg[512];
    long len = 0;
    while (buf[len] != '\0' && buf[len] != '\n' && len < 511) {
        arg[len] = buf[len];
        len++;
    }
    arg[len] = '\0';

    if (arg[0] == '\0') {
        puts("Usage: cat [FILE]...\n");
        return 1;
    }

    fd = open(arg);
    if (fd < 0) {
        printf("cat: open %s failed: %d\n", arg, -fd);
        return 1;
    }

    char data[1024];
    long rd;
    while ((rd = read(fd, data, sizeof data)) > 0) {
        write(1, data, rd);
    }
    close(fd);
    return 0;
}