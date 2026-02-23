// SPDX-License-Identifier: MIT
#include <stdio.h>

extern "C" {

void clearerr(FILE* stream) {
    (void)stream;
}

} // extern "C"
