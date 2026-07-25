// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::aarch64 {

// ARM Generic Timer via AArch64 system registers (CNTFRQ_EL0, CNTVCT_EL0,
// CNTV_TVAL_EL0, CNTV_CTL_EL0). The ARMv7 port reaches the same hardware
// through CP15; only the access path differs.
//
// PPI 27 is the EL1 virtual timer. start.S zeroes CNTVOFF_EL2 during the
// EL2->EL1 drop, so the virtual and physical counters agree.
constexpr uint32_t TIMER_IRQ = 27;

class generic_timer {
public:
    static uint32_t get_frequency() noexcept;
    static uint64_t read_counter() noexcept;
    static void set_timer(uint32_t ticks) noexcept;
    static void enable() noexcept;
    static void disable() noexcept;

    // Reload the countdown after a tick. Called from the IRQ handler.
    static void rearm() noexcept;

    // Program a periodic tick; returns the reload value in counter ticks.
    static uint32_t init_periodic(uint32_t target_hz = 1000) noexcept;
};

} // namespace arch::aarch64
