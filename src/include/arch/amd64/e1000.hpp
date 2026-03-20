// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::amd64 {

// Intel 82540EM Gigabit Ethernet Controller (E1000)
// PCI Vendor: 0x8086, Device: 0x100E
// Used by QEMU -device e1000

class e1000 {
public:
    static bool init() noexcept;
    static void irq_handler() noexcept;
    static void write_reg(uint16_t reg, uint32_t value) noexcept;
    static uint32_t read_reg(uint16_t reg) noexcept;

private:
    static void tx_init() noexcept;
    static void rx_init() noexcept;
    static void link_up() noexcept;
    static void handle_rx() noexcept;
    static void transmit_packet(void* data, uint16_t len) noexcept;
};

} // namespace arch::amd64
