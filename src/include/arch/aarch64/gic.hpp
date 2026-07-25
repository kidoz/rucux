// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::aarch64 {

// GICv2 (GIC-400 on both QEMU `virt` and Amlogic S905). The programming model
// is identical to the ARMv7 port — it is memory mapped, not CP15 — so this
// mirrors arch/armv7/gic.hpp rather than introducing a second model.

namespace gicd_reg {
constexpr uint32_t CTLR = 0x000;       // Control
constexpr uint32_t TYPER = 0x004;      // Interrupt Controller Type
constexpr uint32_t ISENABLER = 0x100;  // Set-Enable (banked per 32 IRQs)
constexpr uint32_t ICENABLER = 0x180;  // Clear-Enable
constexpr uint32_t IPRIORITYR = 0x400; // Priority (1 byte per IRQ)
constexpr uint32_t ITARGETSR = 0x800;  // Processor Targets (1 byte per IRQ)
constexpr uint32_t ICFGR = 0xC00;      // Configuration (2 bits per IRQ)
} // namespace gicd_reg

namespace gicc_reg {
constexpr uint32_t CTLR = 0x000; // Control
constexpr uint32_t PMR = 0x004;  // Priority Mask
constexpr uint32_t IAR = 0x00C;  // Interrupt Acknowledge
constexpr uint32_t EOIR = 0x010; // End of Interrupt
} // namespace gicc_reg

class gic_distributor {
public:
    static void init(uintptr_t base) noexcept;
    static void enable_irq(uint32_t irq) noexcept;
    static void set_priority(uint32_t irq, uint8_t priority) noexcept;
    static uint32_t read(uint32_t reg) noexcept;
    static void write(uint32_t reg, uint32_t value) noexcept;

private:
    static uintptr_t base_;
};

class gic_cpu_interface {
public:
    static void init(uintptr_t base) noexcept;
    static uint32_t acknowledge() noexcept;
    static void end_of_interrupt(uint32_t irq) noexcept;
    static uint32_t read(uint32_t reg) noexcept;
    static void write(uint32_t reg, uint32_t value) noexcept;

private:
    static uintptr_t base_;
};

void gic_init(uintptr_t distributor_base, uintptr_t cpu_interface_base) noexcept;

} // namespace arch::aarch64
