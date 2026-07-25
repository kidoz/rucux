// SPDX-License-Identifier: MIT
#include "syscall_impl.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <uapi/kernel/syscalls.h>
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

pid_t getpid(void) {
    return 1; // stub
}

pid_t getppid(void) {
    return 1; // stub
}

int isatty(int fd) {
    (void)fd;
    return 1; // Everything is a TTY in rucux!
}

unsigned int sleep(unsigned int seconds) {
    struct timespec req = {.tv_sec = seconds, .tv_nsec = 0};
    __syscall(SYS_NANOSLEEP, (long)&req, 0);
    return 0;
}

int usleep(useconds_t usec) {
    struct timespec req = {.tv_sec = usec / 1000000, .tv_nsec = (usec % 1000000) * 1000};
    __syscall(SYS_NANOSLEEP, (long)&req, 0);
    return 0;
}

int gethostname(char* name, size_t len) {
    if (!name || len < 6) return -1;
    strcpy(name, "rucux");
    return 0;
}

int access(const char* pathname, int mode) {
    (void)pathname;
    (void)mode;
    return 0; // stub
}

long sysconf(int name) {
    (void)name;
    return 4096; // 4KB pages
}

int getpagesize(void) {
    return 4096;
}

pid_t wait(int* wstatus) {
    if (wstatus) *wstatus = 0;
    return -1; // stub
}

pid_t waitpid(pid_t pid, int* wstatus, int options) {
    (void)pid;
    (void)options;
    if (wstatus) *wstatus = 0;
    return -1; // stub
}

pid_t fork(void) {
    return -1; // stub
}

int pipe(int pipefd[2]) {
    if (pipefd) {
        pipefd[0] = -1;
        pipefd[1] = -1;
    }
    return -1; // stub
}

int dup2(int oldfd, int newfd) {
    (void)oldfd;
    (void)newfd;
    return -1; // stub
}

int execvp(const char* file, char* const argv[]) {
    (void)file;
    (void)argv;
    return -1; // stub
}

int ftruncate(int fd, off_t length) {
    return (int)__syscall(SYS_FTRUNCATE, (long)fd, (long)length);
}

int truncate(const char* path, off_t length) {
    (void)path;
    (void)length;
    return -1;
}

int fsync(int fd) {
    return (int)__syscall(SYS_FSYNC, (long)fd);
}

int chdir(const char* path) {
    (void)path;
    return -1;
}

int fchmod(int fd, mode_t mode) {
    (void)fd;
    (void)mode;
    return -1;
}

char* getcwd(char* buf, size_t size) {
    (void)buf;
    (void)size;
    return nullptr;
}

int link(const char* oldpath, const char* newpath) {
    (void)oldpath;
    (void)newpath;
    return -1;
}

int readlink(const char* pathname, char* buf, size_t bufsiz) {
    (void)pathname;
    (void)buf;
    (void)bufsiz;
    return -1;
}

int symlink(const char* target, const char* linkpath) {
    (void)target;
    (void)linkpath;
    return -1;
}
}
