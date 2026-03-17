// SPDX-License-Identifier: MIT
#ifndef _LIBC_UNISTD_H
#define _LIBC_UNISTD_H

#include <stddef.h>
#include <sys/types.h>

#define _POSIX_TIMERS 1
#define _POSIX_MONOTONIC_CLOCK 1

#ifdef __cplusplus
extern "C" {
#endif

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
long lseek(int fd, long offset, int whence);
int ftruncate(int fd, off_t length);
int close(int fd);
int unlink(const char* pathname);
uid_t getuid(void);
int isatty(int fd);
unsigned int sleep(unsigned int seconds);
int gethostname(char *name, size_t len);

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
int access(const char *pathname, int mode);

#define _SC_PAGESIZE 30
long sysconf(int name);

int chdir(const char *path);
int fchmod(int fd, mode_t mode);
char *getcwd(char *buf, size_t size);
int link(const char *oldpath, const char *newpath);
int readlink(const char *pathname, char *buf, size_t bufsiz);
int truncate(const char *path, off_t length);
int ftruncate(int fd, off_t length);
int symlink(const char *target, const char *linkpath);

#define _PC_PATH_MAX 1
long pathconf(const char *path, int name);

void _exit(int status) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif // _LIBC_UNISTD_H
