// SPDX-License-Identifier: MIT
#include <time.h>
#include <uapi/kernel/syscalls.h>

extern "C" {

static long __syscall(long num, long a1 = 0, long a2 = 0, long a3 = 0, long a4 = 0, long a5 = 0, long a6 = 0) {
    long ret;
    register long r10 asm("r10") = a4;
    register long r8 asm("r8") = a5;
    register long r9 asm("r9") = a6;
    asm volatile("syscall"
                 : "=a"(ret)
                 : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
                 : "rcx", "r11", "memory");
    return ret;
}

time_t time(time_t* t) {
    struct timespec ts;
    if (__syscall(SYS_CLOCK_GETTIME, CLOCK_REALTIME, (long)&ts) < 0) {
        return -1;
    }
    if (t) *t = ts.tv_sec;
    return ts.tv_sec;
}

char* ctime(const time_t* timep) {
    (void)timep;
    return (char*)"Thu Jan  1 00:00:00 1970\n";
}

struct tm* gmtime(const time_t* timep) {
    static struct tm res;
    return gmtime_r(timep, &res);
}

struct tm* gmtime_r(const time_t* timep, struct tm* result) {
    if (!timep || !result) return nullptr;
    // Super simple stub
    result->tm_sec = *timep % 60;
    result->tm_min = (*timep / 60) % 60;
    result->tm_hour = (*timep / 3600) % 24;
    result->tm_mday = 1;
    result->tm_mon = 0;
    result->tm_year = 70;
    result->tm_wday = 4;
    result->tm_yday = 0;
    result->tm_isdst = 0;
    return result;
}

struct tm* localtime(const time_t* timep) {
    return gmtime(timep);
}

time_t mktime(struct tm* tm) {
    (void)tm;
    return 0;
}

size_t strftime(char* s, size_t max, const char* format, const struct tm* tm) {
    (void)s; (void)max; (void)format; (void)tm;
    return 0; // stub
}

int gettimeofday(struct timeval* tv, void* tz) {
    (void)tz;
    struct timespec ts;
    if (__syscall(SYS_CLOCK_GETTIME, CLOCK_REALTIME, (long)&ts) < 0) {
        return -1;
    }
    if (tv) {
        tv->tv_sec = ts.tv_sec;
        tv->tv_usec = ts.tv_nsec / 1000;
    }
    return 0;
}

int clock_gettime(int clk_id, struct timespec* tp) {
    return (int)__syscall(SYS_CLOCK_GETTIME, clk_id, (long)tp);
}

}
