// SPDX-License-Identifier: MIT
#include <sys/select.h>

extern "C" {

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
    (void)nfds; (void)readfds; (void)writefds; (void)exceptfds; (void)timeout;
    return 0; // stub
}

}
