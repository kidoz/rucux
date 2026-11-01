// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::amd64 {

class pit {
public:
    static void init(uint32_t frequency = 1000) noexcept;
    // BSP boot only, IRQs masked: channel 2 provides startup timing without
    // assuming a CPU frequency or requiring the scheduler's timer ticks.
    static bool wait_10ms() noexcept;
};

} // namespace arch::amd64
