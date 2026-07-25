// SPDX-License-Identifier: MIT
#include "syscall_impl.h"
#include <uapi/kernel/syscalls.h>
#include <unistd.h>

extern "C" {

long lseek(int fd, long offset, int whence) {
    return __syscall(SYS_LSEEK, (long)fd, (long)offset, (long)whence);
}
}
