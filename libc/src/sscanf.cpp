// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <stdarg.h>

extern "C" {

int sscanf(const char* str, const char* format, ...) {
    (void)str; (void)format;
    return 0; // stub
}

}
