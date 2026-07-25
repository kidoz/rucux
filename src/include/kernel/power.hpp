// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace kernel::power {

long sys_power_ctl(uint32_t command) noexcept;

} // namespace kernel::power
