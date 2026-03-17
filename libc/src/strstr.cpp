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

char* strpbrk(const char* s, const char* accept) {
    while (*s) {
        const char *a = accept;
        while (*a) {
            if (*a++ == *s) return (char *)s;
        }
        ++s;
    }
    return nullptr;
}

char* strtok_r(char* str, const char* delim, char** saveptr) {
    if (!str && !saveptr) return nullptr;
    if (!str) str = *saveptr;
    
    str += strspn(str, delim);
    if (!*str) {
        *saveptr = str;
        return nullptr;
    }
    
    char* token = str;
    str = strpbrk(token, delim);
    if (str) {
        *str = '\0';
        *saveptr = str + 1;
    } else {
        *saveptr = token + strlen(token);
    }
    return token;
}

static char* g_strtok_saveptr = nullptr;

char* strtok(char* str, const char* delim) {
    return strtok_r(str, delim, &g_strtok_saveptr);
}

}