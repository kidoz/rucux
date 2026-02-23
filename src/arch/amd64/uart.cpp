// SPDX-License-Identifier: MIT
#include <arch/amd64/io.hpp>
#include <arch/amd64/uart.hpp>

namespace arch::amd64 {

void uart::init(uint16_t port) noexcept {
    outb(port + 1, 0x00); // Disable interrupts
    outb(port + 3, 0x80); // Enable DLAB
    outb(port + 0, 0x03); // Set divisor to 3 (38400 baud)
    outb(port + 1, 0x00);
    outb(port + 3, 0x03); // 8 bits, no parity, 1 stop bit
    outb(port + 2, 0xC7); // Enable FIFO, clear with 14-byte threshold
    outb(port + 4, 0x0B); // IRQs enabled, RTS/DSR set

    // Enable "Received Data Available" interrupt
    outb(port + 1, 0x01);
}

static bool is_transmit_empty(uint16_t port) noexcept {
    return (inb(port + 5) & 0x20) != 0;
}

void uart::putc(char c, uint16_t port) noexcept {
    while (!is_transmit_empty(port))
        ;
    outb(port, static_cast<uint8_t>(c));
}

void uart::write(const char* s, uint16_t port) noexcept {
    for (; *s != '\0'; ++s) {
        putc(*s, port);
    }
}

} // namespace arch::amd64
