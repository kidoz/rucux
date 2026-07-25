// SPDX-License-Identifier: MIT
#include <arch/armv7/uart.hpp>

namespace arch::armv7 {

static volatile uint32_t* g_uart_wfifo = nullptr;
static volatile uint32_t* g_uart_status = nullptr;
static bool g_is_pl011 = false;

void uart::init() noexcept {
    // If not initialized dynamically, default to Odroid C2
    if (!g_uart_wfifo) {
        g_uart_wfifo = reinterpret_cast<uint32_t*>(0xC81004C0);
        g_uart_status = reinterpret_cast<uint32_t*>(0xC81004CC);
        g_is_pl011 = false;
    }
}

void uart::init_dynamic(uintptr_t base, bool is_pl011) noexcept {
    g_is_pl011 = is_pl011;
    if (is_pl011) {
        // PL011 UART: DR is at offset 0x00, FR is at 0x18
        g_uart_wfifo = reinterpret_cast<uint32_t*>(base + 0x00);
        g_uart_status = reinterpret_cast<uint32_t*>(base + 0x18);
    } else {
        // Amlogic UART: WFIFO at 0x00, STATUS at 0x0C
        g_uart_wfifo = reinterpret_cast<uint32_t*>(base + 0x00);
        g_uart_status = reinterpret_cast<uint32_t*>(base + 0x0C);
    }
}

void uart::putc(char c) noexcept {
    if (!g_uart_wfifo) return;
    
    if (g_is_pl011) {
        // PL011 TXFF (Transmit FIFO full) is bit 5 (0x20) of FR
        while ((*g_uart_status & 0x20)) {
            // Spin
        }
    } else {
        // Amlogic TX_FULL is bit 21
        while ((*g_uart_status & (1 << 21))) {
            // Spin
        }
    }
    *g_uart_wfifo = static_cast<uint32_t>(c);
}

void uart::write(const char* s) noexcept {
    for (; *s != '\0'; ++s) {
        putc(*s);
    }
}

} // namespace arch::armv7
