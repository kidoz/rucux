// SPDX-License-Identifier: MIT
#ifndef _STDINT_H
#define _STDINT_H

#ifdef __cplusplus
#include <lib/stdint.hpp>

using lib::int16_t;
using lib::int32_t;
using lib::int64_t;
using lib::int8_t;
using lib::intmax_t;
using lib::intptr_t;
using lib::uint16_t;
using lib::uint32_t;
using lib::uint64_t;
using lib::uint8_t;
using lib::uintmax_t;
using lib::uintptr_t;

#else

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;

typedef long intptr_t;
typedef unsigned long uintptr_t;

typedef long long intmax_t;
typedef unsigned long long uintmax_t;

#endif

#define INT64_MAX 9223372036854775807LL
#define INT64_MIN (-INT64_MAX - 1LL)
#define INT32_MAX 2147483647
#define UINT8_MAX 255
#define UINT16_MAX 65535
#define UINT32_MAX 4294967295U
#define UINT64_MAX 18446744073709551615ULL
#define UINTPTR_MAX 18446744073709551615ULL
#define SIZE_MAX 18446744073709551615ULL

#endif // _STDINT_H