// SPDX-License-Identifier: MIT
#include <errno.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <uapi/kernel/syscalls.h>

namespace {
int checked_result(long result) {
    if (result < 0) {
        errno = static_cast<int>(-result);
        return -1;
    }
    return static_cast<int>(result);
}
} // namespace

extern "C" int stat(const char* path, struct stat* buffer) {
    return checked_result(syscall(SYS_STAT, reinterpret_cast<long>(path), reinterpret_cast<long>(buffer)));
}

extern "C" int fstat(int fd, struct stat* buffer) {
    return checked_result(syscall(SYS_FSTAT, fd, reinterpret_cast<long>(buffer)));
}
