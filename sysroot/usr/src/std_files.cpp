// SPDX-License-Identifier: MIT
#include <stdio.h>

extern "C" {
struct _FILE {
    int fd;
};

static _FILE _stdin = {0};
static _FILE _stdout = {1};
static _FILE _stderr = {2};

FILE* stdin = (FILE*)&_stdin;
FILE* stdout = (FILE*)&_stdout;
void* stderr = (void*)&_stderr;
}
