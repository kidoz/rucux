// SPDX-License-Identifier: MIT
// These interfaces are declared by the SDK and referenced by the selected
// ports, but have no kernel implementation yet. Report that fact to callers;
// never fabricate a descriptor or claim that an operation succeeded.
#include <errno.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include <sys/file.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/types.h>

extern "C" {
int eventfd(unsigned int, int) {
    errno = ENOSYS;
    return -1;
}
int eventfd_read(int, eventfd_t*) {
    errno = ENOSYS;
    return -1;
}
int eventfd_write(int, eventfd_t) {
    errno = ENOSYS;
    return -1;
}
int timerfd_create(int, int) {
    errno = ENOSYS;
    return -1;
}
int timerfd_settime(int, int, const struct itimerspec*, struct itimerspec*) {
    errno = ENOSYS;
    return -1;
}
int timerfd_gettime(int, struct itimerspec*) {
    errno = ENOSYS;
    return -1;
}
int signalfd(int, const sigset_t*, int) {
    errno = ENOSYS;
    return -1;
}
int flock(int, int) {
    errno = ENOSYS;
    return -1;
}
struct msghdr;
ssize_t sendmsg(int, const struct msghdr*, int) {
    errno = ENOSYS;
    return -1;
}
ssize_t recvmsg(int, struct msghdr*, int) {
    errno = ENOSYS;
    return -1;
}
FILE* open_memstream(char**, size_t*) {
    errno = ENOSYS;
    return nullptr;
}
}
