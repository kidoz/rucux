// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::amd64 {

struct gdt_descriptor {
    uint16_t limit;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_pointer {
    uint16_t size;
    uint64_t offset;
} __attribute__((packed));

void gdt_init() noexcept;

} // namespace arch::amd64
