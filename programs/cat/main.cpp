// SPDX-License-Identifier: MIT
#include <stdint.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>

int main(int argc, char** argv) {
    if (argc < 2) return 1;

    for (int i = 1; i < argc; ++i) {
        int fd = syscall(SYS_OPEN, (long)argv[i], 0);
        if (fd < 0) continue;

        char buffer[1024];
        long bytes;
        while ((bytes = syscall(SYS_READ, fd, (long)buffer, sizeof(buffer))) > 0) {
            syscall(SYS_WRITE, 1, (long)buffer, bytes);
        }
        syscall(SYS_CLOSE, fd);
    }
    return 0;
}
