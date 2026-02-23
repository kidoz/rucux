// SPDX-License-Identifier: MIT
#ifndef _POLL_H
#define _POLL_H

#ifdef __cplusplus
extern "C" {
#endif

#define POLLIN 0x001
#define POLLOUT 0x004
#define POLLERR 0x008

struct pollfd {
    int fd;
    short events;
    short revents;
};

int poll(struct pollfd* fds, unsigned int nfds, int timeout);

#ifdef __cplusplus
}
#endif

#endif // _POLL_H