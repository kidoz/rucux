// SPDX-License-Identifier: MIT
#include <arch/aarch64/psci.hpp>
#include <kernel/power.hpp>
#include <kernel/print.hpp>

namespace kernel::power {

void reboot() noexcept {
    kernel::print("Rebooting via PSCI...\n");
    arch::aarch64::psci::system_reset();
}

void shutdown() noexcept {
    kernel::print("Powering off via PSCI...\n");
    arch::aarch64::psci::system_off();
}

} // namespace kernel::power
