// SPDX-License-Identifier: MIT
#include "syscall_impl.h"
#include <poll.h>
#include <uapi/kernel/syscalls.h>

extern "C" {

int poll(struct pollfd* fds, unsigned int nfds, int timeout) {
    return (int)__syscall(SYS_POLL, (long)fds, (long)nfds, (long)timeout);
}
}