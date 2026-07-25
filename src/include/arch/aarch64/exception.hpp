// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::aarch64 {

// Installs the vector table in VBAR_EL1 and unmasks IRQs.
void exceptions_init() noexcept;

// Number of IRQs dispatched since boot. Used by the QEMU smoke check to prove
// the timer is actually firing rather than merely being programmed.
uint64_t irq_count() noexcept;

// Allow the timer tick to drive scheduler::schedule(). Must not be called
// before the scheduler is initialized and boot is ready to be preempted.
void enable_preemption() noexcept;

} // namespace arch::aarch64
