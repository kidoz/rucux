// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace kernel::fdt {

struct device_info {
    bool has_dwmac;
    bool has_mali450;
    bool has_usb;
    bool has_watchdog;
    bool has_hw_rng;
};

// Initialize FDT parser with the blob passed by bootloader
// Returns true if valid FDT is found.
bool init(void* fdt_blob);

// Retrieve the first memory region
bool get_memory(uint64_t* base, uint64_t* size);

// Retrieve UART base address and type
bool get_uart(uintptr_t* base, bool* is_pl011);

// Retrieve GIC base addresses
bool get_gic(uintptr_t* dist_base, uintptr_t* cpu_base);

// Retrieve available devices for init
device_info get_device_info();

} // namespace kernel::fdt
