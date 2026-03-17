// SPDX-License-Identifier: MIT
#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct stat {
    off_t st_size;
    int st_mode;
    uid_t st_uid;
    time_t st_mtime;
};

#define S_ISREG(m) 1
#define S_ISDIR(m) 0
#define S_ISBLK(m) 0
#define S_ISCHR(m) 0

#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100
#define S_IRWXU (S_IRUSR | S_IWUSR | S_IXUSR)

#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010
#define S_IRWXG (S_IRGRP | S_IWGRP | S_IXGRP)

#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001
#define S_IRWXO (S_IROTH | S_IWOTH | S_IXOTH)

int stat(const char* pathname, struct stat* statbuf);
int fstat(int fd, struct stat* statbuf);
int mkdir(const char* pathname, mode_t mode);

#ifdef __cplusplus
}
#endif

#endif // _SYS_STAT_H