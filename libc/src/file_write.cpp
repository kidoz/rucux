// SPDX-License-Identifier: MIT
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/syscall.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>

namespace {
long checked_result(long result) {
    if (result < 0) {
        errno = static_cast<int>(-result);
        return -1;
    }
    return result;
}
} // namespace

extern "C" ssize_t write(int fd, const void* buffer, size_t size) {
    return checked_result(syscall(SYS_WRITE, fd, reinterpret_cast<long>(buffer), static_cast<long>(size)));
}

extern "C" int fcntl(int fd, int command, ...) {
    long argument = 0;
    if (command == F_SETFL || command == F_SETFD) {
        va_list args;
        va_start(args, command);
        argument = va_arg(args, int);
        va_end(args);
    }
    return static_cast<int>(checked_result(syscall(SYS_FCNTL, fd, command, argument)));
}
