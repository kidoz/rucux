// SPDX-License-Identifier: MIT
#include "syscall_impl.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>

extern "C" {

struct _DIR {
    int fd;
    struct dirent entry;
};

DIR* opendir(const char* name) {
    int fd = open(name, O_RDONLY);
    if (fd < 0) return nullptr;

    _DIR* dir = (_DIR*)malloc(sizeof(_DIR));
    if (!dir) {
        close(fd);
        return nullptr;
    }
    dir->fd = fd;
    return (DIR*)dir;
}

DIR* fdopendir(int fd) {
    if (fd < 0) return nullptr;
    _DIR* dir = (_DIR*)malloc(sizeof(_DIR));
    if (!dir) return nullptr;
    dir->fd = fd;
    return (DIR*)dir;
}

struct dirent* readdir(DIR* dirp) {
    if (!dirp) return nullptr;
    _DIR* dir = (_DIR*)dirp;

    long ret = __syscall(SYS_GETDENTS, (long)dir->fd, (long)&dir->entry, sizeof(struct dirent));
    if (ret <= 0) return nullptr;
    return &dir->entry;
}

int closedir(DIR* dirp) {
    if (!dirp) return -1;
    _DIR* dir = (_DIR*)dirp;
    int ret = close(dir->fd);
    free(dir);
    return ret;
}

} // extern "C"
