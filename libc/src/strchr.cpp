// SPDX-License-Identifier: MIT
#include <string.h>

extern "C" {

char* strchr(const char* s, int c) {
    while (*s != (char)c) {
        if (!*s++) {
            return NULL;
        }
    }
    return (char*)s;
}
}