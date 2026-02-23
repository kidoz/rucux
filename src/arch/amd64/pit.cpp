// SPDX-License-Identifier: MIT
#include <arch/amd64/io.hpp>
#include <arch/amd64/pit.hpp>

namespace arch::amd64 {

void pit::init(uint32_t frequency) noexcept {
    if (frequency == 0) frequency = 1;
    uint32_t divisor = 1193180 / frequency;
    if (divisor > 65535) divisor = 65535;

    outb(0x43, 0x36); // Command: Channel 0, lobyte/hibyte, Mode 3, Binary
    io_wait();
    outb(0x40, static_cast<uint8_t>(divisor & 0xFF));
    io_wait();
    outb(0x40, static_cast<uint8_t>((divisor >> 8) & 0xFF));
}

} // namespace arch::amd64
