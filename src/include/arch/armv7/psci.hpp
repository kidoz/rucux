// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::armv7 {

// Power State Coordination Interface (PSCI)
// Used to bring secondary cores online on ARM platforms.
// PSCI calls are made via SMC (Secure Monitor Call) or HVC (Hypervisor Call).

namespace psci_fn {
constexpr uint32_t PSCI_VERSION = 0x84000000;
constexpr uint32_t CPU_ON_32 = 0x84000003;
constexpr uint32_t CPU_OFF = 0x84000002;
constexpr uint32_t SYSTEM_RESET = 0x84000009;
} // namespace psci_fn

// PSCI return codes
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
    // Get PSCI version (returns packed major.minor or NOT_SUPPORTED)
    static int32_t version() noexcept;

    // Wake a secondary CPU.
    // target_cpu: MPIDR affinity value for the target core
    // entry_point: physical address where the core starts executing
    // context_id: passed to the core in r0 on entry
    static int32_t cpu_on(uint32_t target_cpu, uintptr_t entry_point, uint32_t context_id) noexcept;

    // Power off the calling CPU (does not return on success)
    static void cpu_off() noexcept;

private:
    // Invoke PSCI via SMC (conduit auto-detected; default to SMC)
    static int32_t smc_call(uint32_t fn, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) noexcept;
};

} // namespace arch::armv7
