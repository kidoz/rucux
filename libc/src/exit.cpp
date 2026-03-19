// SPDX-License-Identifier: MIT
#include <stdlib.h>
#include <unistd.h>
#include <uapi/kernel/syscalls.h>
#include "syscall_impl.h"

extern "C" {

void* __dso_handle = nullptr;

int __cxa_atexit(void (*func)(void*), void* arg, void* dso_handle) {
    (void)func;
    (void)arg;
    (void)dso_handle;
    return 0; // stub
}

void exit(int status) {
    _exit(status);
}
}
