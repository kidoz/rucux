// SPDX-License-Identifier: MIT
#include <string.h>

extern "C" {

size_t strcspn(const char* s, const char* reject) {
    size_t count = 0;
    while (*s) {
        if (strchr(reject, *s)) {
            return count;
        }
        s++;
        count++;
    }
    return count;
}

size_t strspn(const char* s, const char* accept) {
    size_t count = 0;
    while (*s) {
        if (!strchr(accept, *s)) {
            return count;
        }
        s++;
        count++;
    }
    return count;
}
}