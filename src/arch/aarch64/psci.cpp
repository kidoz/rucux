// SPDX-License-Identifier: MIT
#include <arch/aarch64/psci.hpp>

// Conduit comes from board.platform.psci.conduit via meson. Defaulting to HVC
// matches QEMU `virt`; the Odroid C2 manifest selects SMC.
#ifndef RUCUX_PSCI_HVC
#define RUCUX_PSCI_HVC 1
#endif

namespace arch::aarch64 {

namespace {

int64_t hvc_call(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3) noexcept {
    register uint64_t x0 asm("x0") = fn;
    register uint64_t x1 asm("x1") = a1;
    register uint64_t x2 asm("x2") = a2;
    register uint64_t x3 asm("x3") = a3;
    asm volatile("hvc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3) : "memory");
    return static_cast<int64_t>(x0);
}

int64_t smc_call(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3) noexcept {
    register uint64_t x0 asm("x0") = fn;
    register uint64_t x1 asm("x1") = a1;
    register uint64_t x2 asm("x2") = a2;
    register uint64_t x3 asm("x3") = a3;
    asm volatile("smc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3) : "memory");
    return static_cast<int64_t>(x0);
}

} // namespace

bool psci::uses_hvc() noexcept {
    return RUCUX_PSCI_HVC != 0;
}

int64_t psci::call(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3) noexcept {
    return uses_hvc() ? hvc_call(fn, a1, a2, a3) : smc_call(fn, a1, a2, a3);
}

int32_t psci::version() noexcept {
    return static_cast<int32_t>(call(psci_fn::PSCI_VERSION));
}

int32_t psci::cpu_on(uint64_t target_mpidr, uintptr_t entry_point, uint64_t context_id) noexcept {
    return static_cast<int32_t>(call(psci_fn::CPU_ON, target_mpidr, entry_point, context_id));
}

void psci::cpu_off() noexcept {
    call(psci_fn::CPU_OFF);
}

void psci::system_off() noexcept {
    call(psci_fn::SYSTEM_OFF);
    for (;;)
        asm volatile("wfi");
}

void psci::system_reset() noexcept {
    call(psci_fn::SYSTEM_RESET);
    for (;;)
        asm volatile("wfi");
}

} // namespace arch::aarch64
