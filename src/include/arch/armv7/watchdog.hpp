// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>

namespace arch::armv7::watchdog {

void init(uint32_t timeout_ms) noexcept;
void feed() noexcept;

} // namespace arch::armv7::watchdog
