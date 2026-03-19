// SPDX-License-Identifier: MIT
#ifndef _SYS_TIME_H
#define _SYS_TIME_H

#include <time.h>
#include <sys/select.h>

#ifdef __cplusplus
extern "C" {
#endif

// struct timeval is defined in time.h

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

int gettimeofday(struct timeval* tv, void* tz);
int utimes(const char *filename, const struct timeval times[2]);

#ifdef __cplusplus
}
#endif

#endif // _SYS_TIME_H
