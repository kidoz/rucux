// SPDX-License-Identifier: MIT
#include <arch/armv7/gic.hpp>
#include <kernel/print.hpp>

namespace arch::armv7 {

// ─── GIC Distributor ───────────────────────────────────────────────────────

uintptr_t gic_distributor::base_ = 0;

uint32_t gic_distributor::read(uint32_t reg) noexcept {
    return *reinterpret_cast<volatile uint32_t*>(base_ + reg);
}

void gic_distributor::write(uint32_t reg, uint32_t value) noexcept {
    *reinterpret_cast<volatile uint32_t*>(base_ + reg) = value;
}

void gic_distributor::init(uintptr_t base) noexcept {
    base_ = base;

    // Disable distributor while configuring
    write(gicd_reg::CTLR, 0);

    // Read number of supported IRQ lines
    uint32_t typer = read(gicd_reg::TYPER);
    uint32_t num_irqs = ((typer & 0x1F) + 1) * 32;

    // Disable all interrupts
    for (uint32_t i = 0; i < num_irqs / 32; ++i)
        write(gicd_reg::ICENABLER + i * 4, 0xFFFFFFFF);

    // Set all priorities to lowest (0xFF = lowest priority)
    for (uint32_t i = 0; i < num_irqs; ++i)
        *reinterpret_cast<volatile uint8_t*>(base_ + gicd_reg::IPRIORITYR + i) = 0xFF;

    // Route all SPIs (IRQ 32+) to CPU 0
    for (uint32_t i = 32; i < num_irqs; ++i)
        *reinterpret_cast<volatile uint8_t*>(base_ + gicd_reg::ITARGETSR + i) = 0x01;

    // Configure all SPIs as level-triggered
    for (uint32_t i = 2; i < num_irqs / 16; ++i)
        write(gicd_reg::ICFGR + i * 4, 0x00000000);

    // Enable distributor
    write(gicd_reg::CTLR, 1);

    kernel::print("GIC Distributor: {} IRQs, base=0x{x}\n", num_irqs, reinterpret_cast<void*>(base));
}

void gic_distributor::enable_irq(uint32_t irq) noexcept {
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    write(gicd_reg::ISENABLER + reg * 4, 1 << bit);
}

void gic_distributor::disable_irq(uint32_t irq) noexcept {
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    write(gicd_reg::ICENABLER + reg * 4, 1 << bit);
}

void gic_distributor::set_priority(uint32_t irq, uint8_t priority) noexcept {
    *reinterpret_cast<volatile uint8_t*>(base_ + gicd_reg::IPRIORITYR + irq) = priority;
}

void gic_distributor::set_target(uint32_t irq, uint8_t cpu_mask) noexcept {
    *reinterpret_cast<volatile uint8_t*>(base_ + gicd_reg::ITARGETSR + irq) = cpu_mask;
}

void gic_distributor::send_sgi(uint8_t target_cpu, uint8_t sgi_id) noexcept {
    // SGIR: TargetListFilter=0 (use target list), CPUTargetList in bits [23:16], INTID in [3:0]
    uint32_t val = (static_cast<uint32_t>(1 << target_cpu) << 16) | (sgi_id & 0x0F);
    write(gicd_reg::SGIR, val);
}

// ─── GIC CPU Interface ────────────────────────────────────────────────────

uintptr_t gic_cpu_interface::base_ = 0;

uint32_t gic_cpu_interface::read(uint32_t reg) noexcept {
    return *reinterpret_cast<volatile uint32_t*>(base_ + reg);
}

void gic_cpu_interface::write(uint32_t reg, uint32_t value) noexcept {
    *reinterpret_cast<volatile uint32_t*>(base_ + reg) = value;
}

void gic_cpu_interface::init(uintptr_t base) noexcept {
    base_ = base;

    // Set priority mask to accept all priorities
    write(gicc_reg::PMR, 0xFF);

    // Enable CPU interface (group 0 + group 1)
    write(gicc_reg::CTLR, 1);

    kernel::print("GIC CPU Interface: base=0x{x}\n", reinterpret_cast<void*>(base));
}

uint32_t gic_cpu_interface::acknowledge() noexcept {
    return read(gicc_reg::IAR);
}

void gic_cpu_interface::end_of_interrupt(uint32_t irq) noexcept {
    write(gicc_reg::EOIR, irq);
}

// ─── High-level init ───────────────────────────────────────────────────────

void gic_init(uintptr_t distributor_base, uintptr_t cpu_interface_base) noexcept {
    gic_distributor::init(distributor_base);
    gic_cpu_interface::init(cpu_interface_base);
}

} // namespace arch::armv7
