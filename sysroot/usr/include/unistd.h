// SPDX-License-Identifier: MIT
#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

ssize_t read(int fd, void* buf, size_t count);
ssize_t write(int fd, const void* buf, size_t count);
ssize_t pread(int fd, void* buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset);
long lseek(int fd, long offset, int whence);
int ftruncate(int fd, off_t length);
int close(int fd);
int unlink(const char* pathname);
uid_t getuid(void);

#define _SC_PAGESIZE 30
long sysconf(int name);

void _exit(int status) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif // _UNISTD_H
