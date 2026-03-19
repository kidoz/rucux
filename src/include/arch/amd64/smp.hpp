// SPDX-License-Identifier: MIT
#pragma once
#include <arch/amd64/acpi.hpp>
#include <stdint.h>

namespace arch::amd64 {

// Boot all Application Processors listed in the MADT.
// Must be called after APIC init and with a valid PML4.
void smp_boot_aps(const acpi::madt_info& info, uintptr_t pml4_phys) noexcept;

} // namespace arch::amd64
