// SPDX-License-Identifier: MIT
#include <poll.h>

extern "C" {

int poll(struct pollfd* fds, unsigned int nfds, int timeout) {
    (void)fds;
    (void)nfds;
    (void)timeout;
    return -1; // stub
}
}