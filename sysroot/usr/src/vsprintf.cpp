// SPDX-License-Identifier: MIT
#include <stdarg.h>
#include <stdio.h>

extern "C" {

int vsprintf(char* str, const char* format, void* ap) {
    (void)str;
    (void)format;
    (void)ap;
    return 0; // stub
}
}
