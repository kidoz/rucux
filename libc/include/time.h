// SPDX-License-Identifier: MIT
#ifndef _LIBC_TIME_H
#define _LIBC_TIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef long time_t;
typedef long clock_t;

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

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

struct timeval {
    time_t tv_sec;
    long tv_usec;
};

time_t time(time_t* t);
char* ctime(const time_t* timep);
struct tm* gmtime(const time_t* timep);
struct tm* gmtime_r(const time_t* timep, struct tm* result);
struct tm* localtime(const time_t* timep);
struct tm* localtime_r(const time_t* timep, struct tm* result);
time_t mktime(struct tm* tm);
size_t strftime(char* s, size_t max, const char* format, const struct tm* tm);

#include <locale.h>
size_t strftime_l(char *s, size_t max, const char *format, const struct tm *tm, locale_t loc);

clock_t clock(void);
double difftime(time_t time1, time_t time0);
char* asctime(const struct tm* timeptr);
int timespec_get(struct timespec* ts, int base);
#define TIME_UTC 1

int gettimeofday(struct timeval* tv, void* tz);

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
int clock_gettime(int clk_id, struct timespec* tp);

#ifdef __cplusplus
}
#endif

#endif // _LIBC_TIME_H
