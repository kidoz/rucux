// SPDX-License-Identifier: MIT
#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#include <stddef.h>

typedef long ssize_t;
typedef long off_t;
typedef long off64_t;
typedef int pid_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned int u_int;
typedef unsigned char u_char;
typedef unsigned int mode_t;
typedef long time_t;

typedef struct {
    unsigned long fds_bits[1024 / (8 * sizeof(long))];
} fd_set;

#define FD_SETSIZE 1024

#ifdef __cplusplus
extern "C" {
#endif

void* __builtin_memset(void* s, int c, size_t n);

#ifdef __cplusplus
}
#endif

#define FD_ZERO(s) do { __builtin_memset((s), 0, sizeof(fd_set)); } while(0)
#define FD_SET(d, s)   ((s)->fds_bits[(d)/(8*sizeof(long))] |= (1UL << ((d)%(8*sizeof(long)))))
#define FD_CLR(d, s)   ((s)->fds_bits[(d)/(8*sizeof(long))] &= ~(1UL << ((d)%(8*sizeof(long)))))
#define FD_ISSET(d, s) !!((s)->fds_bits[(d)/(8*sizeof(long))] & (1UL << ((d)%(8*sizeof(long)))))

#endif // _SYS_TYPES_H
