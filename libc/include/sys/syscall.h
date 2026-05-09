// SPDX-License-Identifier: MIT
#ifndef _LIBC_SYS_SYSCALL_H
#define _LIBC_SYS_SYSCALL_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
long syscall(long num, long a1 = 0, long a2 = 0, long a3 = 0, long a4 = 0, long a5 = 0, long a6 = 0);
#else
long syscall(long num, long a1, long a2, long a3, long a4, long a5, long a6);
#endif

#ifdef __cplusplus
}
#endif

#endif // _LIBC_SYS_SYSCALL_H
