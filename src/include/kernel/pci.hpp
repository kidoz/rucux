// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace kernel::pci {

struct pci_device {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint32_t bar0;
    uint32_t bar1;
    uint32_t bar2;
    uint32_t bar3;
    uint32_t bar4;
    uint32_t bar5;
    uint8_t interrupt_line;
};

void init() noexcept;

// Finds the first device matching the vendor and device ID
bool find_device(uint16_t vendor_id, uint16_t device_id, pci_device& out_dev) noexcept;

// Reads a 32-bit configuration register
uint32_t read_config_32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) noexcept;

// Writes a 32-bit configuration register
void write_config_32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) noexcept;

} // namespace kernel::pci
