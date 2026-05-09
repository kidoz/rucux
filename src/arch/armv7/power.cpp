// SPDX-License-Identifier: MIT
#include <arch/armv7/power.hpp>

#include <generated_arm_platform.hpp>
#include <arch/armv7/psci.hpp>

namespace arch::armv7::power {
namespace {

enum class shutdown_method : uint32_t {
    PSCI = 0,
    SEMIHOST = 1,
};

constexpr uint32_t SEMIHOST_SYS_EXIT = 0x18U;
constexpr uint32_t SEMIHOST_ADP_STOPPED_APPLICATION_EXIT = 0x20026U;

[[noreturn]] void semihost_shutdown() noexcept {
    register uint32_t r0 asm("r0") = SEMIHOST_SYS_EXIT;
    register uint32_t r1 asm("r1") = SEMIHOST_ADP_STOPPED_APPLICATION_EXIT;

    // On ARMv7/A32 QEMU semihosting uses the standard SVC trap, not the
    // ARMv8-era HLT encoding that faults as undefined on Cortex-A15.
    asm volatile("svc #0x123456"
                 :
                 : "r"(r0), "r"(r1)
                 : "memory");

    while (true) {
        asm volatile("wfi");
    }
}

void idle_forever() noexcept {
    while (true) {
        asm volatile("wfi");
    }
}

} // namespace

void reboot() noexcept {
    if (platform::POWER_SHUTDOWN_METHOD == static_cast<uint32_t>(shutdown_method::SEMIHOST)) {
        semihost_shutdown();
    }
    psci::system_reset();
    idle_forever();
}

void power_off() noexcept {
    if (platform::POWER_SHUTDOWN_METHOD == static_cast<uint32_t>(shutdown_method::SEMIHOST)) {
        semihost_shutdown();
    }
    psci::system_off();
    idle_forever();
}

} // namespace arch::armv7::power
