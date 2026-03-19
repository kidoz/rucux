// SPDX-License-Identifier: MIT
#include <sys/epoll.h>
#include <uapi/kernel/syscalls.h>
#include "syscall_impl.h"

extern "C" {

int epoll_create(int size) {
    return (int)__syscall(SYS_EPOLL_CREATE, (long)size);
}

int epoll_create1(int flags) {
    return (int)__syscall(SYS_EPOLL_CREATE, (long)flags); // Stub fallback
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event) {
    return (int)__syscall(SYS_EPOLL_CTL, (long)epfd, (long)op, (long)fd, (long)event);
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout) {
    return (int)__syscall(SYS_EPOLL_WAIT, (long)epfd, (long)events, (long)maxevents, (long)timeout);
}

} // extern "C"