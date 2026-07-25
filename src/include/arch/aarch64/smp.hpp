// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::aarch64 {

// Kernel stack size for each secondary core. Must match the value smp.S is
// assembled with (AARCH64_AP_STACK_SIZE).
constexpr uint32_t AP_STACK_SIZE = 16384;

// Highest secondary core index supported by the static stack array.
constexpr uint32_t MAX_AP_CPUS = 8;

// Bring secondary cores online via PSCI CPU_ON. num_cpus is the total core
// count including the boot CPU.
void smp_boot_aps(uint32_t num_cpus) noexcept;

} // namespace arch::aarch64
