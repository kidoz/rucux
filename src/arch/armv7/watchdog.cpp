// SPDX-License-Identifier: MIT
#include <arch/armv7/watchdog.hpp>
#include <kernel/print.hpp>

namespace arch::armv7::watchdog {

// Amlogic Watchdog Base Address
constexpr uintptr_t WDT_BASE = 0xC11098D0;

// Watchdog Registers
constexpr uintptr_t WDT_TC = WDT_BASE + 0x00;    // Terminal Count Register
constexpr uintptr_t WDT_RESET = WDT_BASE + 0x04; // Reset Register
constexpr uintptr_t WDT_CTRL = WDT_BASE + 0x08;  // Control Register

// Control bit flags
constexpr uint32_t WDT_CTRL_ENABLE = (1 << 22);

// Magic value to feed the watchdog
constexpr uint32_t WDT_RESET_MAGIC = 0; // Standard write 0 to reset count on some Meson variants

static inline void write_reg(uintptr_t addr, uint32_t value) noexcept {
    *reinterpret_cast<volatile uint32_t*>(addr) = value;
}

static inline uint32_t read_reg(uintptr_t addr) noexcept {
    return *reinterpret_cast<volatile uint32_t*>(addr);
}

void init(uint32_t timeout_ms) noexcept {
    kernel::print("watchdog: Initializing Amlogic Watchdog (timeout: {} ms)...\n", timeout_ms);

    // The Amlogic watchdog uses the 24MHz crystal clock or a pre-divided 1MHz clock depending on the variant.
    // For simplicity, we assume the system clock divider is configured such that setting the Terminal Count
    // directly correlates with the timeout. The driver in Linux sets WDT_TC = count.

    // Disable watchdog
    write_reg(WDT_CTRL, read_reg(WDT_CTRL) & ~WDT_CTRL_ENABLE);

    // Set terminal count. We need a real calculation based on clock, but for now we set a generic high value.
    // If we assume a typical 100KHz clock, 100,000 = 1s.
    uint32_t count = timeout_ms * 100;
    write_reg(WDT_TC, count | (count << 16)); // Sometimes the count is mirrored

    // Enable watchdog
    write_reg(WDT_CTRL, read_reg(WDT_CTRL) | WDT_CTRL_ENABLE);

    kernel::print("watchdog: Enabled.\n");
}

void feed() noexcept {
    // Write 0 to the RESET register to reset the counter
    write_reg(WDT_RESET, WDT_RESET_MAGIC);
}

} // namespace arch::armv7::watchdog
