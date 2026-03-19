// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace kernel {

struct timespec {
    int64_t tv_sec;
    long tv_nsec;
};

struct timeval {
    int64_t tv_sec;
    long tv_usec;
};

class time_manager {
public:
    static void init() noexcept;
    static void tick() noexcept;
    static uint64_t get_ticks() noexcept;
    static int sys_clock_gettime(int clk_id, timespec* tp) noexcept;
    static int sys_gettimeofday(timeval* tv, void* tz) noexcept;
    static int sys_nanosleep(const timespec* req, timespec* rem) noexcept;
};

} // namespace kernel
