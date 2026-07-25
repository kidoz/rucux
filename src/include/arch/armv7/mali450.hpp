// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>

namespace arch::armv7::mali450 {

// Mali-450 MMIO Base for Amlogic S905 (Odroid C2)
constexpr uintptr_t MALI_BASE = 0xC9000000;

// Core Offsets (Utgard Architecture)
constexpr uint32_t GP_OFFSET = 0x00000;
constexpr uint32_t GP_MMU_OFFSET = 0x04000;

constexpr uint32_t PP0_OFFSET = 0x08000;
constexpr uint32_t PP1_OFFSET = 0x0A000;
constexpr uint32_t PP2_OFFSET = 0x0C000;

constexpr uint32_t PP0_MMU_OFFSET = 0x04800;
constexpr uint32_t PP1_MMU_OFFSET = 0x04C00;
constexpr uint32_t PP2_MMU_OFFSET = 0x05000;

constexpr uint32_t BCAST_OFFSET = 0x10000;
constexpr uint32_t DLBU_OFFSET = 0x14000;
constexpr uint32_t L2_CACHE_OFFSET = 0x18000;
constexpr uint32_t PMU_OFFSET = 0x16000;

// Essential Registers (Relative to core offset)
constexpr uint32_t REG_CORE_VERSION = 0x0000;
constexpr uint32_t REG_CORE_STATUS = 0x000C; // Example for PP
constexpr uint32_t REG_CORE_COMMAND = 0x0004;

// L2 Cache Registers
constexpr uint32_t L2_REG_SIZE = 0x0004;
constexpr uint32_t L2_REG_STATUS = 0x000C;

void init() noexcept;

} // namespace arch::armv7::mali450
