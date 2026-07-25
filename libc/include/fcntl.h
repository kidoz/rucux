// SPDX-License-Identifier: MIT
#ifndef _FCNTL_H
#define _FCNTL_H

#ifdef __cplusplus
extern "C" {
#endif

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR 0x0002
#define O_CREAT 0x0040
#define O_EXCL 0x0080
#define O_TRUNC 0x0200
#define O_APPEND 0x0400
#define O_NONBLOCK 0x0800
#define O_CLOEXEC 02000000
#define O_DIRECTORY 00200000
#define O_NOFOLLOW 00400000

#define AT_FDCWD -100
#define AT_REMOVEDIR 0x200

#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4

int open(const char* path, int flags, ...);
int openat(int dirfd, const char* pathname, int flags, ...);
int unlinkat(int dirfd, const char* pathname, int flags);
int fcntl(int fd, int cmd, ...);

#ifdef __cplusplus
}
#endif

#endif // _FCNTL_H
