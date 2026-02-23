// SPDX-License-Identifier: MIT
#include <string.h>

extern "C" {

char* strrchr(const char* s, int c) {
    const char* last = NULL;
    while (*s) {
        if (*s == (char)c) {
            last = s;
        }
        s++;
    }
    if (*s == (char)c) {
        last = s;
    }
    return (char*)last;
}
}
