// SPDX-License-Identifier: MIT
#include <stdlib.h>

extern "C" {

long strtol(const char* nptr, char** endptr, int base) {
    (void)nptr;
    (void)endptr;
    (void)base;
    return 0; // stub
}

unsigned long strtoul(const char* nptr, char** endptr, int base) {
    (void)nptr;
    (void)endptr;
    (void)base;
    return 0; // stub
}

long long strtoll(const char* nptr, char** endptr, int base) {
    (void)nptr;
    (void)endptr;
    (void)base;
    return 0; // stub
}

unsigned long long strtoull(const char* nptr, char** endptr, int base) {
    (void)nptr;
    (void)endptr;
    (void)base;
    return 0; // stub
}
}