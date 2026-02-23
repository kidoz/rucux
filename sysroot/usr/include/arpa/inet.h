// SPDX-License-Identifier: MIT
#ifndef _ARPA_INET_H
#define _ARPA_INET_H

#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

// Add basic stubs if needed by LibreSSL
char* inet_ntoa(struct in_addr in);
int inet_pton(int af, const char* src, void* dst);
const char* inet_ntop(int af, const void* src, char* dst, socklen_t size);

#ifdef __cplusplus
}
#endif

#endif // _ARPA_INET_H