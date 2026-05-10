// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

#define SYS_EXIT 0
#define SYS_WRITE 1
#define SYS_READ 2
#define SYS_OPEN 3
#define SYS_CLOSE 4
#define SYS_GETDENTS 5
#define SYS_IPC_SEND 6
#define SYS_IPC_RECV 7
#define SYS_OUTB 8
#define SYS_INB 9
#define SYS_IRQ_WAIT 10
#define SYS_MMAP 11
#define SYS_MUNMAP 12
#define SYS_SOCKET 13
#define SYS_BIND 14
#define SYS_LISTEN 15
#define SYS_ACCEPT 16
#define SYS_CONNECT 17
#define SYS_SEND 18
#define SYS_RECV 19
#define SYS_CLONE 20
#define SYS_FUTEX 21
#define SYS_YIELD 22
#define SYS_IOCTL 23
#define SYS_CLOCK_GETTIME 24
#define SYS_POLL 25
#define SYS_SELECT 26
#define SYS_EPOLL_CREATE 27
#define SYS_EPOLL_CTL 28
#define SYS_EPOLL_WAIT 29
#define SYS_MPROTECT 30
#define SYS_MSYNC 31
#define SYS_MADVISE 32
#define SYS_LSEEK 33
#define SYS_STAT 34
#define SYS_FSTAT 35
#define SYS_FTRUNCATE 36
#define SYS_FSYNC 37
#define SYS_FCNTL 38
#define SYS_SETSOCKOPT 39
#define SYS_GETSOCKOPT 40
#define SYS_GETSOCKNAME 41
#define SYS_GETPEERNAME 42
#define SYS_SENDTO 43
#define SYS_RECVFROM 44
#define SYS_SIGACTION 45
#define SYS_KILL 46
#define SYS_SIGPROCMASK 47
#define SYS_NANOSLEEP 48
#define SYS_GETTIMEOFDAY 49
#define SYS_IPC_CALL  50   // Fast IPC: send + wait for reply (register-based)
#define SYS_IPC_REPLY 51   // Fast IPC: reply to caller + resume them
#define SYS_SPAWN     52   // Process creation: spawn a new process from an ELF file
