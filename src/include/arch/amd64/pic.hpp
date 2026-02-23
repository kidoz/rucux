// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::amd64 {

class pic {
public:
    static constexpr uint16_t PIC1_COMMAND = 0x20;
    static constexpr uint16_t PIC1_DATA = 0x21;
    static constexpr uint16_t PIC2_COMMAND = 0xA0;
    static constexpr uint16_t PIC2_DATA = 0xA1;

    static void init(uint8_t offset1 = 0x20, uint8_t offset2 = 0x28) noexcept;
    static void send_eoi(uint8_t irq) noexcept;
    static void mask(uint8_t irq) noexcept;
    static void unmask(uint8_t irq) noexcept;
};

} // namespace arch::amd64
