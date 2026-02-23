// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::amd64 {

class pit {
public:
    static void init(uint32_t frequency = 1000) noexcept;
};

} // namespace arch::amd64
