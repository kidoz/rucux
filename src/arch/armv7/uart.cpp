// SPDX-License-Identifier: MIT
#include <arch/armv7/uart.hpp>

namespace arch::armv7 {

// UART MMIO base for Odroid C2 is different, but for now, we'll use a placeholder.
// We'll update this once we have more hardware-specific info.
static volatile uint32_t* const UART_BASE = reinterpret_cast<uint32_t*>(0xC81004C0);

void uart::init() noexcept {
    // Basic init code for S905 UART would go here.
}

void uart::putc(char c) noexcept {
    // Wait for TX FIFO not full and then write.
    // Placeholder for now.
    *UART_BASE = static_cast<uint32_t>(c);
}

void uart::write(const char* s) noexcept {
    for (; *s != '\0'; ++s) {
        putc(*s);
    }
}

} // namespace arch::armv7
