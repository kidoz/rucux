// SPDX-License-Identifier: MIT
#include <string.h>

extern "C" {

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; ++haystack) {
        if (*haystack == *needle) {
            const char *h = haystack, *n = needle;
            while (*h && *n && *h == *n) {
                ++h;
                ++n;
            }
            if (!*n) return (char*)haystack;
        }
    }
    return nullptr;
}
}