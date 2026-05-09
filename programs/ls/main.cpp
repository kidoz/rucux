// SPDX-License-Identifier: MIT
#include <stddef.h>
#include <stdint.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>

struct dirent {
    char name[128];
    uint32_t inode;
};

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "/";
    int fd = syscall(SYS_OPEN, (long)path, 0);
    if (fd < 0) return 1;

    dirent d;
    while (syscall(SYS_GETDENTS, fd, (long)&d, sizeof(dirent)) > 0) {
        const char* s = d.name;
        size_t len = 0;
        while (s[len])
            len++;
        syscall(SYS_WRITE, 1, (long)s, (long)len);
        syscall(SYS_WRITE, 1, (long)"  ", 2);
    }
    syscall(SYS_WRITE, 1, (long)"\n", 1);
    syscall(SYS_CLOSE, fd);
    return 0;
}
