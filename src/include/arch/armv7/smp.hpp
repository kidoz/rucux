// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::armv7 {

// Boot secondary cores via PSCI CPU_ON.
// num_cpus: total CPU count from device tree or platform info
void smp_boot_aps(uint32_t num_cpus) noexcept;

} // namespace arch::armv7
