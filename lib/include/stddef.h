// SPDX-License-Identifier: MIT
#ifndef _LIBC_STDDEF_H
#define _LIBC_STDDEF_H

#ifdef __cplusplus
#include <lib/stddef.hpp>

using lib::nullptr_t;
using lib::ptrdiff_t;
using lib::size_t;

#ifndef NULL
#define NULL 0
#endif

#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

#else

typedef unsigned long size_t;
typedef long ptrdiff_t;
#define NULL ((void*)0)

#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

#endif // __cplusplus

typedef struct {
  long long __max_align_ll __attribute__((__aligned__(__alignof__(long long))));
  long double __max_align_ld __attribute__((__aligned__(__alignof__(long double))));
} max_align_t;

#endif // _LIBC_STDDEF_H
