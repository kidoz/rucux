// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::amd64 {

class uart {
public:
    static constexpr uint16_t COM1 = 0x3F8;

    static void init(uint16_t port = COM1) noexcept;
    static void putc(char c, uint16_t port = COM1) noexcept;
    static void write(const char* s, uint16_t port = COM1) noexcept;
};

} // namespace arch::amd64
