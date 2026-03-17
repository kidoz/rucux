// SPDX-License-Identifier: MIT
#include <unistd.h>

extern "C" {

long lseek(int fd, long offset, int whence) {
    (void)fd;
    (void)offset;
    (void)whence;
    return -1; // stub
}
}
