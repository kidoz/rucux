// SPDX-License-Identifier: MIT
#ifndef _LIBC_STDINT_H
#define _LIBC_STDINT_H

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

using lib::int_least8_t;
using lib::uint_least8_t;
using lib::int_least16_t;
using lib::uint_least16_t;
using lib::int_least32_t;
using lib::uint_least32_t;
using lib::int_least64_t;
using lib::uint_least64_t;

using lib::int_fast8_t;
using lib::uint_fast8_t;
using lib::int_fast16_t;
using lib::uint_fast16_t;
using lib::int_fast32_t;
using lib::uint_fast32_t;
using lib::int_fast64_t;
using lib::uint_fast64_t;

#else

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;

typedef int8_t int_least8_t;
typedef uint8_t uint_least8_t;
typedef int16_t int_least16_t;
typedef uint16_t uint_least16_t;
typedef int32_t int_least32_t;
typedef uint32_t uint_least32_t;
typedef int64_t int_least64_t;
typedef uint64_t uint_least64_t;

typedef int8_t int_fast8_t;
typedef uint8_t uint_fast8_t;
typedef int16_t int_fast16_t;
typedef uint16_t uint_fast16_t;
typedef int32_t int_fast32_t;
typedef uint32_t uint_fast32_t;
typedef int64_t int_fast64_t;
typedef uint64_t uint_fast64_t;

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

#define INT8_C(c) c
#define INT16_C(c) c
#define INT32_C(c) c
#define INT64_C(c) c ## LL
#define UINT8_C(c) c
#define UINT16_C(c) c
#define UINT32_C(c) c ## U
#define UINT64_C(c) c ## ULL
#define INTMAX_C(c) c ## LL
#define UINTMAX_C(c) c ## ULL

#endif // _LIBC_STDINT_H