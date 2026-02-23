// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <unistd.h>

extern "C" {

void perror(const char* s) {
    if (s && *s) {
        printf("%s: Unknown error\n", s);
    } else {
        printf("Unknown error\n");
    }
}

int unlink(const char* pathname) {
    (void)pathname;
    return -1; // stub
}

int fileno(FILE* stream) {
    if (!stream) return -1;
    // We cast it back to our internal struct
    struct _FILE {
        int fd;
    };
    return ((_FILE*)stream)->fd;
}

uid_t getuid(void) {
    return 0; // stub
}

long sysconf(int name) {
    (void)name;
    return 4096; // 4KB pages
}

ssize_t pread(int fd, void* buf, size_t count, off_t offset) {
    (void)fd;
    (void)buf;
    (void)count;
    (void)offset;
    return -1; // stub
}

ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset) {
    (void)fd;
    (void)buf;
    (void)count;
    (void)offset;
    return -1; // stub
}

int ftruncate(int fd, off_t length) {
    (void)fd;
    (void)length;
    return -1; // stub
}
}
