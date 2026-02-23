// SPDX-License-Identifier: MIT
#ifndef _TIME_H
#define _TIME_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

struct timeval {
    time_t tv_sec;
    long tv_usec;
};

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

time_t time(time_t* tloc);
struct tm* gmtime_r(const time_t* timep, struct tm* result);
struct tm* gmtime(const time_t* timep);

#ifdef __cplusplus
}
#endif

#endif // _TIME_H