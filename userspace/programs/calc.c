#include <nvlibc.h>

int main(void) {
    int a = 123456789;
    int b = 7;
    int mul = a * b;
    int div = a / b;
    int mod = a % b;
    const char *digits = "0123456789";
    char num[24];
    int idx = 0;
    int value = div;
    if (value == 0) {
        num[idx++] = '0';
    }
    while (value > 0) {
        num[idx++] = digits[value % 10];
        value = value / 10;
    }
    puts("div: ");
    for (int i = idx - 1; i >= 0; i--) {
        putchar(num[i]);
    }
    putchar('\n');
    return mul + mod;
}