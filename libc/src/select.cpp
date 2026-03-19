// SPDX-License-Identifier: MIT
#include <sys/select.h>
#include <uapi/kernel/syscalls.h>
#include "syscall_impl.h"

extern "C" {

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
    return (int)__syscall(SYS_SELECT, (long)nfds, (long)readfds, (long)writefds, (long)exceptfds, (long)timeout);
}

}
