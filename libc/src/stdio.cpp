// SPDX-License-Identifier: MIT
#include <stdarg.h>
#include <stdio.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>

extern "C" {

struct _FILE {
    int fd;
};

int printf(const char* format, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    write(1, buf, ret);
    return ret;
}

int puts(const char* s) {
    if (!s) return -1;
    size_t len = 0;
    while (s[len])
        len++;
    write(1, s, len);
    write(1, "\n", 1);
    return 1;
}

int putchar(int c) {
    char ch = (char)c;
    write(1, &ch, 1);
    return c;
}

int fprintf(FILE* stream, const char* format, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    if (!stream) return -1;
    write(((_FILE*)stream)->fd, buf, ret);
    return ret;
}

int dprintf(int fd, const char* format, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    write(fd, buf, ret);
    return ret;
}

int vfprintf(FILE* stream, const char* format, va_list ap) {
    char buf[1024];
    int ret = vsnprintf(buf, sizeof(buf), format, ap);
    if (!stream) return -1;
    write(((_FILE*)stream)->fd, buf, ret);
    return ret;
}

int setvbuf(FILE* stream, char* buf, int mode, size_t size) {
    (void)stream;
    (void)buf;
    (void)mode;
    (void)size;
    return 0; // stub
}

void setbuf(FILE* stream, char* buf) {
    (void)stream;
    (void)buf;
}
}
