// SPDX-License-Identifier: MIT
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>

extern "C" {

struct message {
    uint32_t sender;
    uint32_t type;
    uint64_t data[4];
};

static long __syscall(long num, long a1 = 0, long a2 = 0, long a3 = 0, long a4 = 0, long a5 = 0, long a6 = 0) {
    long ret;
    register long r10 asm("r10") = a4;
    register long r8 asm("r8") = a5;
    register long r9 asm("r9") = a6;
    asm volatile("syscall"
                 : "=a"(ret)
                 : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
                 : "rcx", "r11", "memory");
    return ret;
}

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

void _exit(int status) {
    __syscall(SYS_EXIT, (long)status);
    while (1) {
    }
}

} // extern "C"
