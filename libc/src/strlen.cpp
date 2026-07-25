// SPDX-License-Identifier: MIT
#include <string.h>

extern "C" {

size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len]) {
        len++;
    }
    return len;
}
}
