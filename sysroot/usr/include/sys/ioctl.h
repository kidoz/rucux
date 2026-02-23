// SPDX-License-Identifier: MIT
#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

#ifdef __cplusplus
extern "C" {
#endif

#define TCGETS 0x5401
#define TCSETS 0x5402
#define TIOCSTI 0x5412

int ioctl(int fd, unsigned long request, ...);

#ifdef __cplusplus
}
#endif

#endif // _SYS_IOCTL_H
