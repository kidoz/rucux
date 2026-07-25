// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::armv7 {

// GICv2 (Generic Interrupt Controller v2)
// Distributor: global, routes interrupts to CPU interfaces
// CPU Interface: per-CPU, handles interrupt acknowledge/EOI

// GIC Distributor register offsets
namespace gicd_reg {
constexpr uint32_t CTLR = 0x000;       // Control
constexpr uint32_t TYPER = 0x004;      // Interrupt Controller Type
constexpr uint32_t ISENABLER = 0x100;  // Set-Enable (banked per 32 IRQs)
constexpr uint32_t ICENABLER = 0x180;  // Clear-Enable
constexpr uint32_t ISPENDR = 0x200;    // Set-Pending
constexpr uint32_t ICPENDR = 0x280;    // Clear-Pending
constexpr uint32_t IPRIORITYR = 0x400; // Priority (1 byte per IRQ)
constexpr uint32_t ITARGETSR = 0x800;  // Processor Targets (1 byte per IRQ)
constexpr uint32_t ICFGR = 0xC00;      // Configuration (2 bits per IRQ)
constexpr uint32_t SGIR = 0xF00;       // Software Generated Interrupt
} // namespace gicd_reg

// GIC CPU Interface register offsets
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
    static void disable_irq(uint32_t irq) noexcept;
    static void set_priority(uint32_t irq, uint8_t priority) noexcept;
    static void set_target(uint32_t irq, uint8_t cpu_mask) noexcept;
    static void send_sgi(uint8_t target_cpu, uint8_t sgi_id) noexcept;
    static uint32_t read(uint32_t reg) noexcept;
    static void write(uint32_t reg, uint32_t value) noexcept;

private:
    static uintptr_t base_;
};

class gic_cpu_interface {
public:
    static void init(uintptr_t base) noexcept;
    static uint32_t acknowledge() noexcept; // Returns interrupt ID
    static void end_of_interrupt(uint32_t irq) noexcept;
    static uint32_t read(uint32_t reg) noexcept;
    static void write(uint32_t reg, uint32_t value) noexcept;

private:
    static uintptr_t base_;
};

// High-level GIC initialization
void gic_init(uintptr_t distributor_base, uintptr_t cpu_interface_base) noexcept;

} // namespace arch::armv7
