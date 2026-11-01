// SPDX-License-Identifier: MIT
#include <arch/amd64/io.hpp>
#include <arch/amd64/pit.hpp>
#include <kernel/sync/spinlock.hpp>

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

bool pit::wait_10ms() noexcept {
    constexpr uint16_t count = 11932;
    const auto original = inb(0x61);
    outb(0x61, original & ~3U); // Gate low, speaker disabled.
    outb(0x43, 0xB2);
    outb(0x42, count & 255);
    outb(0x42, count >> 8);
    outb(0x61, (original & ~2U) | 1U);
    bool elapsed = false;
    for (unsigned spin = 0; spin < 10000000; ++spin) {
        if (inb(0x61) & 0x20) {
            elapsed = true;
            break;
        }
        kernel::cpu_relax();
    }
    outb(0x61, original);
    return elapsed;
}

} // namespace arch::amd64
