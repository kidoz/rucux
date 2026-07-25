// SPDX-License-Identifier: MIT
#include <setjmp.h>

extern "C" {

int setjmp(jmp_buf env) {
    (void)env;
    return 0;
}

void longjmp(jmp_buf env, int val) {
    (void)env;
    (void)val;
    while (1)
        ;
}

int sigsetjmp(sigjmp_buf env, int savesigs) {
    (void)env;
    (void)savesigs;
    return 0;
}

void siglongjmp(sigjmp_buf env, int val) {
    (void)env;
    (void)val;
    while (1)
        ;
}
}
