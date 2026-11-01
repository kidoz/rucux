// SPDX-License-Identifier: MIT
#pragma once
#include <arch/armv7/topology.hpp>
#include <stdint.h>

namespace arch::armv7 {

// Boot secondary cores via PSCI CPU_ON.
// num_cpus: total CPU count from device tree or platform info
bool smp_boot_aps(const cpu_topology& topology) noexcept;
void smp_release_aps() noexcept;

} // namespace arch::armv7
