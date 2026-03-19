// SPDX-License-Identifier: MIT
#ifndef _UCHAR_H
#define _UCHAR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int mbstate_t;

#ifndef __cplusplus
typedef uint16_t char16_t;
typedef uint32_t char32_t;
#endif

#ifdef __cplusplus
}
#endif

#endif // _UCHAR_H