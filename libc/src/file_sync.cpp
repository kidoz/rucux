// SPDX-License-Identifier: MIT
#include <errno.h>
#include <sys/syscall.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>

namespace {
int checked_result(long result) {
    if (result < 0) {
        errno = static_cast<int>(-result);
        return -1;
    }
    return static_cast<int>(result);
}
} // namespace

extern "C" int ftruncate(int fd, off_t length) {
    static_assert(sizeof(off_t) <= sizeof(long));
    if (length < 0) {
        errno = EINVAL;
        return -1;
    }
    return checked_result(syscall(SYS_FTRUNCATE, fd, static_cast<long>(length)));
}

extern "C" int fsync(int fd) {
    return checked_result(syscall(SYS_FSYNC, fd));
}
