// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>

namespace arch::armv7::hw_rng {

// Read 32 bits of hardware-generated entropy
uint32_t read() noexcept;

void init() noexcept;

} // namespace arch::armv7::hw_rng
