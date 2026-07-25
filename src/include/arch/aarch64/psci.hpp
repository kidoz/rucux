// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::aarch64 {

// Power State Coordination Interface.
//
// The conduit is platform-specific and cannot be probed safely: issuing SMC
// where the firmware expects HVC (or vice versa) traps. QEMU `virt` routes PSCI
// through HVC; the Odroid C2's ARM Trusted Firmware uses SMC. It therefore
// comes from the board manifest via RUCUX_PSCI_HVC.
//
// SMC64 function IDs (0xC4...) are used rather than the SMC32 (0x84...) ones
// the ARMv7 port issues, so 64-bit entry points and MPIDR values are passed
// intact.
namespace psci_fn {
constexpr uint32_t PSCI_VERSION = 0x84000000;  // version is always SMC32
constexpr uint32_t CPU_ON = 0xC4000003;        // SMC64
constexpr uint32_t CPU_OFF = 0x84000002;       // takes no arguments
constexpr uint32_t SYSTEM_OFF = 0x84000008;
constexpr uint32_t SYSTEM_RESET = 0x84000009;
} // namespace psci_fn

namespace psci_ret {
constexpr int32_t SUCCESS = 0;
constexpr int32_t NOT_SUPPORTED = -1;
constexpr int32_t INVALID_PARAMETERS = -2;
constexpr int32_t DENIED = -3;
constexpr int32_t ALREADY_ON = -4;
constexpr int32_t ON_PENDING = -5;
constexpr int32_t INTERNAL_FAILURE = -6;
} // namespace psci_ret

class psci {
public:
    static int32_t version() noexcept;

    // Wake a secondary core. target_mpidr is the core's MPIDR_EL1 affinity
    // value; entry_point is a physical address; context_id arrives in x0.
    static int32_t cpu_on(uint64_t target_mpidr, uintptr_t entry_point, uint64_t context_id) noexcept;

    static void cpu_off() noexcept;
    [[noreturn]] static void system_off() noexcept;
    [[noreturn]] static void system_reset() noexcept;

    // True when calls are routed through HVC rather than SMC.
    static bool uses_hvc() noexcept;

private:
    static int64_t call(uint64_t fn, uint64_t a1 = 0, uint64_t a2 = 0, uint64_t a3 = 0) noexcept;
};

} // namespace arch::aarch64
