// SPDX-License-Identifier: MIT
#include <sys/time.h>
#include <time.h>

extern "C" {

time_t time(time_t* tloc) {
    if (tloc) *tloc = 0;
    return 0; // stub
}

int gettimeofday(struct timeval* tv, struct timezone* tz) {
    (void)tz;
    if (tv) {
        tv->tv_sec = 0;
        tv->tv_usec = 0;
    }
    return 0; // stub
}

struct tm* gmtime_r(const time_t* timep, struct tm* result) {
    (void)timep;
    if (result) {
        result->tm_sec = 0;
        result->tm_min = 0;
        result->tm_hour = 0;
        result->tm_mday = 1;
        result->tm_mon = 0;
        result->tm_year = 70;
        result->tm_wday = 4;
        result->tm_yday = 0;
        result->tm_isdst = 0;
    }
    return result;
}

static struct tm g_tm;
struct tm* gmtime(const time_t* timep) {
    return gmtime_r(timep, &g_tm);
}

} // extern "C"
