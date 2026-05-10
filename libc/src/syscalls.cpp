// SPDX-License-Identifier: MIT
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>

#include "syscall_impl.h"

extern "C" {

int open(const char* path, int flags, ...) {
    return (int)__syscall(SYS_OPEN, (long)path, (long)flags);
}

ssize_t read(int fd, void* buf, size_t count) {
    return (ssize_t)__syscall(SYS_READ, (long)fd, (long)buf, (long)count);
}

ssize_t write(int fd, const void* buf, size_t count) {
    return (ssize_t)__syscall(SYS_WRITE, (long)fd, (long)buf, (long)count);
}

int close(int fd) {
    return (int)__syscall(SYS_CLOSE, (long)fd);
}

#include <stdarg.h>

int ioctl(int fd, unsigned long request, ...) {
    va_list args;
    va_start(args, request);
    void* argp = va_arg(args, void*);
    va_end(args);
    return (int)__syscall(SYS_IOCTL, (long)fd, (long)request, (long)argp);
}

void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
    return (void*)__syscall(SYS_MMAP, (long)addr, (long)length, (long)prot, (long)flags, (long)fd, (long)offset);
}

int munmap(void* addr, size_t length) {
    return (int)__syscall(SYS_MUNMAP, (long)addr, (long)length);
}

int mprotect(void *addr, size_t len, int prot) {
    return (int)__syscall(SYS_MPROTECT, (long)addr, (long)len, (long)prot);
}

int msync(void *addr, size_t length, int flags) {
    return (int)__syscall(SYS_MSYNC, (long)addr, (long)length, (long)flags);
}

int madvise(void *addr, size_t length, int advice) {
    return (int)__syscall(SYS_MADVISE, (long)addr, (long)length, (long)advice);
}

int mincore(void *addr, size_t length, unsigned char *vec) {
    (void)addr;
    // For now, assume all pages are in core (1).
    // length is in bytes. We'd normally fill vec based on pages.
    size_t num_pages = (length + 4095) / 4096;
    for (size_t i = 0; i < num_pages; ++i) {
        vec[i] = 1;
    }
    return 0; // stub
}

void _exit(int status) {
    __syscall(SYS_EXIT, (long)status);
    while (1) {
    }
}

long spawn(const char* path) {
    return (long)__syscall(SYS_SPAWN, (long)path);
}

} // extern "C"
