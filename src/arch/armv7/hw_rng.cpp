// SPDX-License-Identifier: MIT
#include <arch/armv7/hw_rng.hpp>
#include <kernel/print.hpp>

namespace arch::armv7::hw_rng {

// Amlogic Hardware RNG Base Address
constexpr uintptr_t RNG_BASE = 0xC8834000;

// The single data register at offset 0
constexpr uintptr_t RNG_DATA = RNG_BASE + 0x00;

uint32_t read() noexcept {
    // Read a 32-bit random value from the hardware register
    return *reinterpret_cast<volatile uint32_t*>(RNG_DATA);
}

void init() noexcept {
    kernel::print("hw_rng: Initializing Amlogic Hardware RNG...\n");

    // Perform a test read to ensure it's responding
    uint32_t sample1 = read();
    uint32_t sample2 = read();

    kernel::print("hw_rng: Samples: 0x{:08x}, 0x{:08x}\n", sample1, sample2);
}

} // namespace arch::armv7::hw_rng
