// SPDX-License-Identifier: MIT
#include <arch/aarch64/timer.hpp>
#include <kernel/print.hpp>

namespace arch::aarch64 {

namespace {
uint32_t g_timer_period = 0;
} // namespace

uint32_t generic_timer::get_frequency() noexcept {
    uint64_t freq = 0;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    return static_cast<uint32_t>(freq);
}

uint64_t generic_timer::read_counter() noexcept {
    uint64_t val = 0;
    asm volatile("isb" ::: "memory");
    asm volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
}

void generic_timer::set_timer(uint32_t ticks) noexcept {
    auto val = static_cast<uint64_t>(ticks);
    asm volatile("msr cntv_tval_el0, %0" ::"r"(val) : "memory");
}

void generic_timer::enable() noexcept {
    uint64_t ctl = 1; // ENABLE=1, IMASK=0
    asm volatile("msr cntv_ctl_el0, %0" ::"r"(ctl) : "memory");
    asm volatile("isb" ::: "memory");
}

void generic_timer::disable() noexcept {
    uint64_t ctl = 0;
    asm volatile("msr cntv_ctl_el0, %0" ::"r"(ctl) : "memory");
    asm volatile("isb" ::: "memory");
}

void generic_timer::rearm() noexcept {
    if (g_timer_period != 0)
        set_timer(g_timer_period);
}

uint32_t generic_timer::init_periodic(uint32_t target_hz) noexcept {
    uint32_t freq = get_frequency();
    if (freq == 0) {
        kernel::print("Generic Timer: CNTFRQ_EL0 is 0, cannot init\n");
        return 0;
    }
    if (target_hz == 0)
        target_hz = 1000;

    g_timer_period = freq / target_hz;

    set_timer(g_timer_period);
    enable();

    kernel::print("Generic Timer: freq={} Hz, period={} ticks ({} Hz)\n", freq, g_timer_period, target_hz);
    return g_timer_period;
}

} // namespace arch::aarch64
