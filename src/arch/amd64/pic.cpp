// SPDX-License-Identifier: MIT
#include <arch/amd64/io.hpp>
#include <arch/amd64/pic.hpp>

namespace arch::amd64 {

void pic::init(uint8_t offset1, uint8_t offset2) noexcept {
    uint8_t a1, a2;

    a1 = inb(PIC1_DATA); // Save masks
    a2 = inb(PIC2_DATA);

    // ICW1: Init
    outb(PIC1_COMMAND, 0x11);
    io_wait();
    outb(PIC2_COMMAND, 0x11);
    io_wait();

    // ICW2: Set vector offsets
    outb(PIC1_DATA, offset1);
    io_wait();
    outb(PIC2_DATA, offset2);
    io_wait();

    // ICW3: Master/Slave wiring
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();

    // ICW4: 8086 mode
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    outb(PIC1_DATA, a1); // Restore masks
    outb(PIC2_DATA, a2);
}

void pic::send_eoi(uint8_t irq) noexcept {
    if (irq >= 8) outb(PIC2_COMMAND, 0x20);
    outb(PIC1_COMMAND, 0x20);
}

void pic::mask(uint8_t irq) noexcept {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    value = inb(port) | (1 << irq);
    outb(port, value);
}

void pic::unmask(uint8_t irq) noexcept {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    value = inb(port) & ~(1 << irq);
    outb(port, value);
}

} // namespace arch::amd64
