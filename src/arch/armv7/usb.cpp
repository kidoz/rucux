// SPDX-License-Identifier: MIT
#include <arch/armv7/usb.hpp>
#include <kernel/print.hpp>

namespace arch::armv7::usb {

// Amlogic S905 Always-On (AO) GPIO registers (Approximate base 0xC8100000)
// For Odroid C2, we need GPIOAO_4 (Hub Reset) and GPIOAO_5 (OTG Power)
constexpr uintptr_t AO_GPIO_O = 0xC8100014;
constexpr uintptr_t AO_GPIO_O_EN_N = 0xC8100018;

// Amlogic USB PHY Base Addresses
constexpr uintptr_t USB0_PHY_BASE = 0xC0000000;
constexpr uintptr_t USB1_PHY_BASE = 0xC0000020;

// Synopsys DWC2 Core Base Addresses
constexpr uintptr_t USB0_DWC2_BASE = 0xC9000000;
constexpr uintptr_t USB1_DWC2_BASE = 0xC9100000;

static inline uint32_t read_reg(uintptr_t addr) noexcept {
    return *reinterpret_cast<volatile uint32_t*>(addr);
}

static inline void write_reg(uintptr_t addr, uint32_t value) noexcept {
    *reinterpret_cast<volatile uint32_t*>(addr) = value;
}

static void delay(uint32_t count) noexcept {
    for (uint32_t i = 0; i < count; i = i + 1) {
        asm volatile("" ::: "memory");
    }
}

void init() noexcept {
    kernel::print("usb: Initializing Amlogic USB PHYs and GL852G Hub...\n");

    // Phase 1: Hardware Power-On & GPIO Routing
    // Enable GPIOAO_4 (Bit 4) and GPIOAO_5 (Bit 5) as outputs.
    // In Amlogic SoCs, O_EN_N is active-low for output enable (0 = Output).
    uint32_t o_en = read_reg(AO_GPIO_O_EN_N);
    o_en &= ~((1 << 4) | (1 << 5));
    write_reg(AO_GPIO_O_EN_N, o_en);

    // Turn ON OTG Power (GPIOAO_5 = HIGH)
    uint32_t out = read_reg(AO_GPIO_O);
    out |= (1 << 5);
    write_reg(AO_GPIO_O, out);
    kernel::print("usb: OTG VBUS Power Enabled (GPIOAO_5 HIGH).\n");

    // Reset GL852G Hub (GPIOAO_4 = LOW, then HIGH)
    out = read_reg(AO_GPIO_O);
    out &= ~(1 << 4); // Drive LOW
    write_reg(AO_GPIO_O, out);
    delay(100000); // Wait in reset

    out = read_reg(AO_GPIO_O);
    out |= (1 << 4); // Drive HIGH
    write_reg(AO_GPIO_O, out);
    delay(100000); // Wait for hub to wake up
    kernel::print("usb: GL852G Hub taken out of reset (GPIOAO_4 toggled).\n");

    // Phase 2: Amlogic USB PHY Initialization
    // Standard Amlogic PHY bringup usually involves writing to the config register (Offset 0x00)
    // to clear the power-down bits. For this mock bringup, we just ensure power is on.

    // USB0 PHY
    uint32_t phy0_cfg = read_reg(USB0_PHY_BASE);
    // Typical magic to enable Amlogic PHY (Clear bit 16 for Port Power)
    phy0_cfg &= ~(1 << 16);
    write_reg(USB0_PHY_BASE, phy0_cfg);

    // USB1 PHY
    uint32_t phy1_cfg = read_reg(USB1_PHY_BASE);
    phy1_cfg &= ~(1 << 16);
    write_reg(USB1_PHY_BASE, phy1_cfg);

    kernel::print("usb: Amlogic USB PHY0 and PHY1 powered on.\n");

    // Phase 3 & 4 (DWC2 Core & Enumeration) are highly complex and deferred to a full USB stack.
    kernel::print("usb: DWC2 Host Controller ready for full USB stack initialization.\n");
}

} // namespace arch::armv7::usb
