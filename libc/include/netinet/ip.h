// SPDX-License-Identifier: MIT
#ifndef _NETINET_IP_H
#define _NETINET_IP_H

#include <stdint.h>
#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

// IP TOS
#define IPTOS_LOWDELAY      0x10
#define IPTOS_THROUGHPUT    0x08
#define IPTOS_RELIABILITY   0x04

#ifdef __cplusplus
}
#endif

#endif // _NETINET_IP_H