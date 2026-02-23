// SPDX-License-Identifier: MIT
#include <arch/amd64/io.hpp>
#include <kernel/pci.hpp>
#include <kernel/print.hpp>

namespace kernel::pci {

#define CONFIG_ADDRESS 0xCF8
#define CONFIG_DATA 0xCFC

uint32_t read_config_32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) noexcept {
    uint32_t address;
    uint32_t lbus = static_cast<uint32_t>(bus);
    uint32_t lslot = static_cast<uint32_t>(device);
    uint32_t lfunc = static_cast<uint32_t>(function);

    address = (uint32_t)((lbus << 16) | (lslot << 11) | (lfunc << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));

    arch::amd64::outl(CONFIG_ADDRESS, address);
    return arch::amd64::inl(CONFIG_DATA);
}

void write_config_32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) noexcept {
    uint32_t address;
    uint32_t lbus = static_cast<uint32_t>(bus);
    uint32_t lslot = static_cast<uint32_t>(device);
    uint32_t lfunc = static_cast<uint32_t>(function);

    address = (uint32_t)((lbus << 16) | (lslot << 11) | (lfunc << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));

    arch::amd64::outl(CONFIG_ADDRESS, address);
    arch::amd64::outl(CONFIG_DATA, value);
}

void init() noexcept {
    kernel::print("PCI: Enumerating buses...\n");
    // Just a basic check of Bus 0
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            uint32_t vendor = read_config_32(bus, device, 0, 0);
            if ((vendor & 0xFFFF) != 0xFFFF) {
                uint32_t class_info = read_config_32(bus, device, 0, 0x08);
                uint8_t class_code = (class_info >> 24) & 0xFF;
                uint8_t subclass = (class_info >> 16) & 0xFF;
                kernel::print("PCI: Bus {}, Device {} - Vendor: {}, Device: {}, Class: {}, Subclass: {}\n", bus, device,
                              vendor & 0xFFFF, vendor >> 16, class_code, subclass);
            }
        }
    }
}

bool find_device(uint16_t vendor_id, uint16_t device_id, pci_device& out_dev) noexcept {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            uint32_t vendor_reg = read_config_32(bus, device, 0, 0);
            if ((vendor_reg & 0xFFFF) == vendor_id && (vendor_reg >> 16) == device_id) {
                out_dev.bus = bus;
                out_dev.device = device;
                out_dev.function = 0; // Assume function 0 for simplicity
                out_dev.vendor_id = vendor_id;
                out_dev.device_id = device_id;

                uint32_t class_info = read_config_32(bus, device, 0, 0x08);
                out_dev.class_code = (class_info >> 24) & 0xFF;
                out_dev.subclass = (class_info >> 16) & 0xFF;
                out_dev.prog_if = (class_info >> 8) & 0xFF;

                out_dev.bar0 = read_config_32(bus, device, 0, 0x10);
                out_dev.bar1 = read_config_32(bus, device, 0, 0x14);
                out_dev.bar2 = read_config_32(bus, device, 0, 0x18);
                out_dev.bar3 = read_config_32(bus, device, 0, 0x1C);
                out_dev.bar4 = read_config_32(bus, device, 0, 0x20);
                out_dev.bar5 = read_config_32(bus, device, 0, 0x24);

                uint32_t intr = read_config_32(bus, device, 0, 0x3C);
                out_dev.interrupt_line = intr & 0xFF;
                return true;
            }
        }
    }
    return false;
}

} // namespace kernel::pci
