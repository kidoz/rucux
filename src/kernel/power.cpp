// SPDX-License-Identifier: MIT
#include <kernel/power.hpp>

#include <kernel/print.hpp>
#include <uapi/kernel/power.h>

#if defined(__x86_64__)
#include <arch/amd64/power.hpp>
#elif defined(__arm__)
#include <arch/armv7/power.hpp>
#else
#error Unsupported architecture for kernel::power
#endif

namespace kernel::power {

long sys_power_ctl(uint32_t command) noexcept {
    switch (command) {
    case RUCUX_POWER_CTL_REBOOT:
        kernel::print("power: reboot requested\n");
#if defined(__x86_64__)
        arch::amd64::power::reboot();
#else
        arch::armv7::power::reboot();
#endif
        return 0;
    case RUCUX_POWER_CTL_POWEROFF:
        kernel::print("power: poweroff requested\n");
#if defined(__x86_64__)
        arch::amd64::power::power_off();
#else
        arch::armv7::power::power_off();
#endif
        return 0;
    default:
        return -1;
    }
}

} // namespace kernel::power

