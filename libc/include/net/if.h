// SPDX-License-Identifier: MIT
#ifndef _NET_IF_H
#define _NET_IF_H

#ifdef __cplusplus
extern "C" {
#endif

struct ifreq {
    char ifr_name[16];
};

#ifdef __cplusplus
}
#endif

#endif // _NET_IF_H
