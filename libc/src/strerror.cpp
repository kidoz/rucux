// SPDX-License-Identifier: MIT
#include <string.h>

extern "C" {

char* strerror(int errnum) {
    (void)errnum;
    return (char*)"Unknown error";
}
}
