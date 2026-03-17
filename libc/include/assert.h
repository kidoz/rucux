// SPDX-License-Identifier: MIT
#ifndef _ASSERT_H
#define _ASSERT_H

#ifdef NDEBUG
#define assert(e) ((void)0)
#else
#define assert(e) ((void)0) // stub
#endif

#endif // _ASSERT_H