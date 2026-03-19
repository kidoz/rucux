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

char* strsignal(int sig) {
    (void)sig;
    return (char*)"Unknown signal";
}

} // extern "C"
