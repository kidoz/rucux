// SPDX-License-Identifier: MIT
#include <stdarg.h>
#include <stdio.h>

extern "C" {

int sprintf(char* str, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsprintf(str, format, ap);
    va_end(ap);
    return ret;
}
}
