#include <nvlibc.h>

int main(void) {
    int n = 5;

    for (int i = 1; i <= n; i++) {
        for (int j = 0; j < i; j++) {
            putchar('*');
        }
        putchar('\n');
    }

    return 0;
}