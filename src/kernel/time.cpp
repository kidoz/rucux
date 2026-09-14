// SPDX-License-Identifier: MIT
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/sync/spinlock.hpp>
#include <kernel/time.hpp>

namespace kernel {

static uint64_t g_ticks = 0;
static irq_spinlock g_time_lock;
// Assuming 1000 Hz tick rate (1 ms per tick)
static constexpr uint32_t TICK_RATE_HZ = 1000;
static constexpr uint64_t NANOS_PER_TICK = 1000000000ULL / TICK_RATE_HZ;

void time_manager::init() noexcept {
    g_ticks = 0;
    kernel::print("Time manager initialized\n");
}

void time_manager::tick() noexcept {
    uint64_t now;
    {
        irq_lock_guard guard(g_time_lock);
        now = ++g_ticks;
    }
    scheduler::scheduler::check_sleepers(now);
}

uint64_t time_manager::get_ticks() noexcept {
    irq_lock_guard guard(g_time_lock);
    return g_ticks;
}

int time_manager::sys_clock_gettime(int clk_id, timespec* tp) noexcept {
    (void)clk_id; // Ignore clock ID for now, just return uptime
    if (!tp) return -1;
    uint64_t current_ticks = get_ticks();
    tp->tv_sec = current_ticks / TICK_RATE_HZ;
    tp->tv_nsec = (current_ticks % TICK_RATE_HZ) * NANOS_PER_TICK;
    return 0;
}

int time_manager::sys_gettimeofday(timeval* tv, void* tz) noexcept {
    (void)tz; // Ignore timezone
    if (!tv) return -1;
    uint64_t current_ticks = get_ticks();
    tv->tv_sec = current_ticks / TICK_RATE_HZ;
    tv->tv_usec = (current_ticks % TICK_RATE_HZ) * (1000000ULL / TICK_RATE_HZ);
    return 0;
}

int time_manager::sys_nanosleep(const timespec* req, timespec* rem) noexcept {
    if (!req || req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L) return -22;
    const uint64_t now = get_ticks();
    const uint64_t fractional = (static_cast<uint64_t>(req->tv_nsec) + NANOS_PER_TICK - 1) / NANOS_PER_TICK;
    if (static_cast<uint64_t>(req->tv_sec) > (UINT64_MAX - now - fractional) / TICK_RATE_HZ) return -22;
    const uint64_t duration = static_cast<uint64_t>(req->tv_sec) * TICK_RATE_HZ + fractional;
    // Syscall entry masks IRQs. Yielding a still-runnable sleeper can keep the
    // CPU in the kernel forever; blocking lets the idle thread enable IRQs.
    if (duration) scheduler::scheduler::sleep_until(now + duration);

    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}

} // namespace kernel
