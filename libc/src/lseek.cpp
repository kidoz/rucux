// SPDX-License-Identifier: MIT
#include <errno.h>
#include <sys/syscall.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>

extern "C" {

long lseek(int fd, long offset, int whence) {
    const long result = syscall(SYS_LSEEK, fd, offset, whence);
    if (result < 0) {
        errno = static_cast<int>(-result);
        return -1;
    }
    return result;
}
}
