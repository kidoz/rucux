// SPDX-License-Identifier: MIT
#include <stdarg.h>
#include <stdio.h>

extern "C" {

int snprintf(char* str, size_t size, const char* format, ...) {
    (void)str;
    (void)size;
    (void)format;
    return 0; // stub
}

int vsnprintf(char* str, size_t size, const char* format, void* ap) {
    (void)str;
    (void)size;
    (void)format;
    (void)ap;
    return 0; // stub
}

} // extern "C"
