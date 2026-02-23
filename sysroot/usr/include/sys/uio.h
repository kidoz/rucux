// SPDX-License-Identifier: MIT
#ifndef _SYS_UIO_H
#define _SYS_UIO_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct iovec {
    void* iov_base;
    size_t iov_len;
};

#ifdef __cplusplus
}
#endif

#endif // _SYS_UIO_H