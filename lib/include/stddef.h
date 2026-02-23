// SPDX-License-Identifier: MIT
#ifndef _STDDEF_H
#define _STDDEF_H

#ifdef __cplusplus
#include <lib/stddef.hpp>

using lib::nullptr_t;
using lib::ptrdiff_t;
using lib::size_t;

#ifndef NULL
#define NULL 0
#endif

#ifndef offsetof
#define offsetof(type, member) ((size_t)&((type*)0)->member)
#endif

#else

typedef unsigned long size_t;
typedef long ptrdiff_t;
#define NULL ((void*)0)

#ifndef offsetof
#define offsetof(type, member) ((size_t)&((type*)0)->member)
#endif

#endif // __cplusplus

#endif // _STDDEF_H
