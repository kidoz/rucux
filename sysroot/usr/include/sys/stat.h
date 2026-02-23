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
};

#define S_ISREG(m) 1
#define S_ISDIR(m) 0
#define S_ISBLK(m) 0
#define S_ISCHR(m) 0

#define S_IRUSR 0400
#define S_IWUSR 0200

int stat(const char* pathname, struct stat* statbuf);
int fstat(int fd, struct stat* statbuf);

#ifdef __cplusplus
}
#endif

#endif // _SYS_STAT_H