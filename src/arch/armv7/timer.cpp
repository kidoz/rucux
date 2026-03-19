// SPDX-License-Identifier: MIT
#include <arch/armv7/timer.hpp>
#include <kernel/print.hpp>

namespace arch::armv7 {

uint32_t generic_timer::get_frequency() noexcept {
    uint32_t freq;
    asm volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(freq)); // CNTFRQ
    return freq;
}

uint64_t generic_timer::read_counter() noexcept {
    uint32_t lo, hi;
    asm volatile("mrrc p15, 1, %0, %1, c14" : "=r"(lo), "=r"(hi)); // CNTVCT
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

void generic_timer::set_timer(uint32_t ticks) noexcept {
    // CNTV_TVAL: set the timer value (counts down from this value)
    asm volatile("mcr p15, 0, %0, c14, c3, 0" :: "r"(ticks)); // CNTV_TVAL
}

void generic_timer::enable() noexcept {
    uint32_t ctl = 1; // ENABLE=1, IMASK=0 (interrupt not masked)
    asm volatile("mcr p15, 0, %0, c14, c3, 1" :: "r"(ctl)); // CNTV_CTL
}

void generic_timer::disable() noexcept {
    uint32_t ctl = 0; // ENABLE=0
    asm volatile("mcr p15, 0, %0, c14, c3, 1" :: "r"(ctl)); // CNTV_CTL
}

// Timer tick count for periodic reload
static uint32_t g_timer_period = 0;

uint32_t generic_timer::init_periodic(uint32_t target_hz) noexcept {
    uint32_t freq = get_frequency();
    if (freq == 0) {
        kernel::print("Generic Timer: CNTFRQ is 0, cannot init\n");
        return 0;
    }

    g_timer_period = freq / target_hz;

    set_timer(g_timer_period);
    enable();

    kernel::print("Generic Timer: freq={} Hz, period={} ticks ({} Hz)\n",
                  freq, g_timer_period, target_hz);
    return g_timer_period;
}

uint32_t timer_get_period() noexcept {
    return g_timer_period;
}

} // namespace arch::armv7
