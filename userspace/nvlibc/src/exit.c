#include <nvlibc.h>

void exit(int code) {
    nv_exit(code);
    for (;;) {
    }
}