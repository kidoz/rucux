// SPDX-License-Identifier: MIT
#include <stdlib.h>
#include <unistd.h>

extern "C" {

void exit(int status) {
    _exit(status);
}
}
