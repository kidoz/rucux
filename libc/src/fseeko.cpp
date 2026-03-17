// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <sys/types.h>

extern "C" {

int fseeko(FILE* stream, off_t offset, int whence) {
    (void)stream;
    (void)offset;
    (void)whence;
    return -1;
}

int fseeko64(FILE* stream, off64_t offset, int whence) {
    (void)stream;
    (void)offset;
    (void)whence;
    return -1;
}

off_t ftello(FILE* stream) {
    (void)stream;
    return -1;
}

off64_t ftello64(FILE* stream) {
    (void)stream;
    return -1;
}

} // extern "C"
