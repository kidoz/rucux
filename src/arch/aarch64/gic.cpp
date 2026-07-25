// SPDX-License-Identifier: MIT
#include <arch/aarch64/gic.hpp>
#include <kernel/print.hpp>

namespace arch::aarch64 {

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

    write(gicd_reg::CTLR, 0);

    uint32_t typer = read(gicd_reg::TYPER);
    uint32_t num_irqs = ((typer & 0x1F) + 1) * 32;

    for (uint32_t i = 0; i < num_irqs / 32; ++i)
        write(gicd_reg::ICENABLER + i * 4, 0xFFFFFFFF);

    for (uint32_t i = 0; i < num_irqs; ++i)
        *reinterpret_cast<volatile uint8_t*>(base_ + gicd_reg::IPRIORITYR + i) = 0xFF;

    // Route SPIs (32+) to CPU 0. SGIs and PPIs are banked per-CPU and have no
    // meaningful target register.
    for (uint32_t i = 32; i < num_irqs; ++i)
        *reinterpret_cast<volatile uint8_t*>(base_ + gicd_reg::ITARGETSR + i) = 0x01;

    for (uint32_t i = 2; i < num_irqs / 16; ++i)
        write(gicd_reg::ICFGR + i * 4, 0x00000000);

    write(gicd_reg::CTLR, 1);

    kernel::print("GIC Distributor: {} IRQs, base={}\n", num_irqs, reinterpret_cast<void*>(base));
}

void gic_distributor::enable_irq(uint32_t irq) noexcept {
    write(gicd_reg::ISENABLER + (irq / 32) * 4, 1u << (irq % 32));
}

void gic_distributor::set_priority(uint32_t irq, uint8_t priority) noexcept {
    *reinterpret_cast<volatile uint8_t*>(base_ + gicd_reg::IPRIORITYR + irq) = priority;
}

// ─── GIC CPU Interface ─────────────────────────────────────────────────────

uintptr_t gic_cpu_interface::base_ = 0;

uint32_t gic_cpu_interface::read(uint32_t reg) noexcept {
    return *reinterpret_cast<volatile uint32_t*>(base_ + reg);
}

void gic_cpu_interface::write(uint32_t reg, uint32_t value) noexcept {
    *reinterpret_cast<volatile uint32_t*>(base_ + reg) = value;
}

void gic_cpu_interface::init(uintptr_t base) noexcept {
    base_ = base;
    // Accept every priority, then enable signalling to the core.
    write(gicc_reg::PMR, 0xFF);
    write(gicc_reg::CTLR, 1);
}

uint32_t gic_cpu_interface::acknowledge() noexcept {
    return read(gicc_reg::IAR);
}

void gic_cpu_interface::end_of_interrupt(uint32_t irq) noexcept {
    write(gicc_reg::EOIR, irq);
}

void gic_init(uintptr_t distributor_base, uintptr_t cpu_interface_base) noexcept {
    gic_distributor::init(distributor_base);
    gic_cpu_interface::init(cpu_interface_base);
}

} // namespace arch::aarch64
