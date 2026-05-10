// SPDX-License-Identifier: MIT
#include <arch/armv7/uart.hpp>

namespace arch::armv7 {

// UART MMIO base for Odroid C2
static volatile uint32_t* const UART_WFIFO = reinterpret_cast<uint32_t*>(0xC81004C0);
static volatile uint32_t* const UART_STATUS = reinterpret_cast<uint32_t*>(0xC81004CC);

void uart::init() noexcept {
    // Basic init code for S905 UART would go here.
}

void uart::putc(char c) noexcept {
    // Wait for TX FIFO not full (bit 21 is TX_FULL)
    while ((*UART_STATUS & (1 << 21))) {
        // Spin
    }
    *UART_WFIFO = static_cast<uint32_t>(c);
}

void uart::write(const char* s) noexcept {
    for (; *s != '\0'; ++s) {
        putc(*s);
    }
}

} // namespace arch::armv7
