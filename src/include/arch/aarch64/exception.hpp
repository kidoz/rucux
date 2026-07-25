// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::aarch64 {

// Installs the vector table in VBAR_EL1 and unmasks IRQs.
void exceptions_init() noexcept;

// Number of IRQs dispatched since boot. Used by the QEMU smoke check to prove
// the timer is actually firing rather than merely being programmed.
uint64_t irq_count() noexcept;

} // namespace arch::aarch64
