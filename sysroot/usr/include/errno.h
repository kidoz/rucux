// SPDX-License-Identifier: MIT
#ifndef _ERRNO_H
#define _ERRNO_H

#ifdef __cplusplus
extern "C" {
#endif

// Just a basic errno implementation for zlib
extern int errno;

#define EINVAL 22
#define ERANGE 34
#define ENOMEM 12
#define ENOENT 2
#define ENOTTY 25
#define ENOTCONN 107
#define EINTR 4
#define EAGAIN 11
#define EINPROGRESS 115
#define EALREADY 114
#define EOVERFLOW 75
#define ECONNRESET 104

#ifdef __cplusplus
}
#endif

#endif // _ERRNO_H
