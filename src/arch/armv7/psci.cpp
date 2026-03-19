// SPDX-License-Identifier: MIT
#include <arch/armv7/psci.hpp>
#include <kernel/print.hpp>

namespace arch::armv7 {

int32_t psci::smc_call(uint32_t fn, uint32_t a1, uint32_t a2, uint32_t a3) noexcept {
    register uint32_t r0 asm("r0") = fn;
    register uint32_t r1 asm("r1") = a1;
    register uint32_t r2 asm("r2") = a2;
    register uint32_t r3 asm("r3") = a3;

    // SMC #0 — Secure Monitor Call to EL3 firmware
    asm volatile("smc #0"
                 : "+r"(r0)
                 : "r"(r1), "r"(r2), "r"(r3)
                 : "memory");

    return static_cast<int32_t>(r0);
}

int32_t psci::version() noexcept {
    return smc_call(psci_fn::PSCI_VERSION);
}

int32_t psci::cpu_on(uint32_t target_cpu, uintptr_t entry_point,
                     uint32_t context_id) noexcept {
    int32_t ret = smc_call(psci_fn::CPU_ON_32, target_cpu,
                           static_cast<uint32_t>(entry_point), context_id);
    if (ret != psci_ret::SUCCESS) {
        kernel::print("PSCI CPU_ON failed: target={}, ret={}\n", target_cpu, ret);
    }
    return ret;
}

void psci::cpu_off() noexcept {
    smc_call(psci_fn::CPU_OFF);
    // Should not return
    while (true)
        asm volatile("wfi");
}

} // namespace arch::armv7
