// SPDX-License-Identifier: MIT
#ifndef _LIBC_SYS_STATVFS_H
#define _LIBC_SYS_STATVFS_H

#ifdef __cplusplus
extern "C" {
#endif

struct statvfs {
    unsigned long f_bsize;
    unsigned long f_frsize;
    unsigned long f_blocks;
    unsigned long f_bfree;
    unsigned long f_bavail;
    unsigned long f_files;
    unsigned long f_ffree;
    unsigned long f_favail;
    unsigned long f_fsid;
    unsigned long f_flag;
    unsigned long f_namemax;
};

int statvfs(const char* path, struct statvfs* buf);
int fstatvfs(int fd, struct statvfs* buf);

#ifdef __cplusplus
}
#endif

#endif // _LIBC_SYS_STATVFS_H
