// SPDX-License-Identifier: MIT
#include <stdarg.h>
#include <stdio.h>

extern "C" {

int snprintf(char* str, size_t size, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, size, format, ap);
    va_end(ap);
    return ret;
}

}
