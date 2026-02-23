// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::armv7 {

class uart {
public:
    static void init() noexcept;
    static void putc(char c) noexcept;
    static void write(const char* s) noexcept;
};

} // namespace arch::armv7
