// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace arch::armv7 {
constexpr uint32_t MAX_CPUS = 8; // GICv2 CPU target-list width.
struct cpu_topology {
    uint32_t mpidrs[MAX_CPUS]{};
    uint32_t count = 0, blob_size = 0;
    bool psci = false, hvc = false;
};
// Bounded parser for enabled CPU affinities and PSCI 0.2+ conduit. No allocation.
bool parse_cpu_topology(const void* blob, size_t available, cpu_topology& result) noexcept;
} // namespace arch::armv7
