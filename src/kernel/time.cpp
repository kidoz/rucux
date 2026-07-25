// SPDX-License-Identifier: MIT
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/time.hpp>

namespace kernel {

static uint64_t g_ticks = 0;
// Assuming 1000 Hz tick rate (1 ms per tick)
static constexpr uint32_t TICK_RATE_HZ = 1000;
static constexpr uint64_t NANOS_PER_TICK = 1000000000ULL / TICK_RATE_HZ;

void time_manager::init() noexcept {
    g_ticks = 0;
    kernel::print("Time manager initialized\n");
}

void time_manager::tick() noexcept {
    g_ticks++;
}

uint64_t time_manager::get_ticks() noexcept {
    return g_ticks;
}

int time_manager::sys_clock_gettime(int clk_id, timespec* tp) noexcept {
    (void)clk_id; // Ignore clock ID for now, just return uptime
    if (!tp) return -1;
    uint64_t current_ticks = g_ticks;
    tp->tv_sec = current_ticks / TICK_RATE_HZ;
    tp->tv_nsec = (current_ticks % TICK_RATE_HZ) * NANOS_PER_TICK;
    return 0;
}

int time_manager::sys_gettimeofday(timeval* tv, void* tz) noexcept {
    (void)tz; // Ignore timezone
    if (!tv) return -1;
    uint64_t current_ticks = g_ticks;
    tv->tv_sec = current_ticks / TICK_RATE_HZ;
    tv->tv_usec = (current_ticks % TICK_RATE_HZ) * (1000000ULL / TICK_RATE_HZ);
    return 0;
}

int time_manager::sys_nanosleep(const timespec* req, timespec* rem) noexcept {
    if (!req) return -1;

    // Very naive sleep implementation for now
    uint64_t sleep_ticks = (req->tv_sec * TICK_RATE_HZ) + (req->tv_nsec / NANOS_PER_TICK);
    uint64_t wake_tick = g_ticks + sleep_ticks;

    // Ideally this should block the thread and wait for a timer interrupt to wake it up
    // For now, we will do a busy wait, yielding the CPU
    while (g_ticks < wake_tick) {
        scheduler::scheduler::yield();
    }

    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}

} // namespace kernel
