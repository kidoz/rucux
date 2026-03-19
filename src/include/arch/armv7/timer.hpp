// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::armv7 {

// ARM Generic Timer (accessed via CP15 on ARMv7)
// Uses the virtual timer (CNTVCT / CNTV_TVAL / CNTV_CTL) for per-CPU scheduling.
// The physical timer is typically reserved for the hypervisor.

class generic_timer {
public:
    // Read the timer frequency (CNTFRQ)
    static uint32_t get_frequency() noexcept;

    // Read the current virtual counter value (CNTVCT, 64-bit)
    static uint64_t read_counter() noexcept;

    // Set the virtual timer to fire after `ticks` counter increments
    static void set_timer(uint32_t ticks) noexcept;

    // Enable/disable the virtual timer interrupt
    static void enable() noexcept;
    static void disable() noexcept;

    // Initialize the timer for periodic interrupts at ~1000 Hz
    // Returns the tick count used per period
    static uint32_t init_periodic(uint32_t target_hz = 1000) noexcept;
};

} // namespace arch::armv7
