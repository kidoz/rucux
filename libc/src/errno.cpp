// SPDX-License-Identifier: MIT
#include <errno.h>

extern "C" {

static int g_errno = 0;

int* __errno_location(void) {
    return &g_errno;
}
}
