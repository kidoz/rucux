// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

// Hosted ATA boundary; only ata.o is compiled with this include directory.
namespace arch::amd64 {
void outb(uint16_t port, uint8_t value) noexcept;
uint8_t inb(uint16_t port) noexcept;
uint16_t inw(uint16_t port) noexcept;
void inw_rep(uint16_t port, void* buffer, uint32_t words) noexcept;
void outw_rep(uint16_t port, const void* buffer, uint32_t words) noexcept;
} // namespace arch::amd64
