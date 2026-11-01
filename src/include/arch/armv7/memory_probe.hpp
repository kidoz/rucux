// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
namespace arch::armv7 {
void memory_probe_on_ap(uint32_t id) noexcept;
bool verify_broadcast_memory() noexcept;
void scheduler_probe() noexcept;
} // namespace arch::armv7
