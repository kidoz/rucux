// SPDX-License-Identifier: MIT
#include <string.h>

extern "C" {

char* strsignal(int sig) {
    (void)sig;
    return (char*)"Unknown signal";
}

} // extern "C"
