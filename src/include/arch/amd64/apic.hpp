// SPDX-License-Identifier: MIT
#pragma once
#include <arch/amd64/acpi.hpp>
#include <stdint.h>

namespace arch::amd64 {

// Local APIC register offsets (memory-mapped)
namespace lapic_reg {
    constexpr uint32_t ID         = 0x020;
    constexpr uint32_t VERSION    = 0x030;
    constexpr uint32_t TPR        = 0x080; // Task Priority
    constexpr uint32_t EOI        = 0x0B0; // End of Interrupt
    constexpr uint32_t SVR        = 0x0F0; // Spurious Interrupt Vector
    constexpr uint32_t ICR_LOW    = 0x300; // Interrupt Command (low 32)
    constexpr uint32_t ICR_HIGH   = 0x310; // Interrupt Command (high 32)
    constexpr uint32_t LVT_TIMER  = 0x320;
    constexpr uint32_t LVT_LINT0  = 0x350;
    constexpr uint32_t LVT_LINT1  = 0x360;
    constexpr uint32_t TIMER_INIT = 0x380; // Timer Initial Count
    constexpr uint32_t TIMER_CURR = 0x390; // Timer Current Count
    constexpr uint32_t TIMER_DIV  = 0x3E0; // Timer Divide Config
} // namespace lapic_reg

// LAPIC Timer modes
constexpr uint32_t LAPIC_TIMER_PERIODIC = (1 << 17);
constexpr uint32_t LAPIC_TIMER_MASKED   = (1 << 16);

// ICR delivery modes
constexpr uint32_t ICR_INIT  = 0x00000500;
constexpr uint32_t ICR_SIPI  = 0x00000600;
constexpr uint32_t ICR_LEVEL = 0x00008000;
constexpr uint32_t ICR_ASSERT = 0x00004000;
constexpr uint32_t ICR_DEASSERT = 0x00000000;

class lapic {
public:
    static void init(uintptr_t base_address) noexcept;
    static void send_eoi() noexcept;
    static uint32_t read(uint32_t reg) noexcept;
    static void write(uint32_t reg, uint32_t value) noexcept;
    static uint8_t id() noexcept;

    // LAPIC timer: periodic mode with given vector and frequency divisor
    static void timer_init(uint8_t vector, uint32_t initial_count, uint8_t divide) noexcept;
    static void timer_stop() noexcept;

    // Send IPI (Inter-Processor Interrupt)
    static void send_ipi(uint8_t target_apic_id, uint32_t icr_low) noexcept;
    static void send_init(uint8_t target_apic_id) noexcept;
    static void send_sipi(uint8_t target_apic_id, uint8_t vector) noexcept;

private:
    static uintptr_t base_;
};

// I/O APIC: routes external interrupts to Local APICs
class ioapic {
public:
    static void init(uintptr_t base_address) noexcept;

    // Route an IRQ to a specific LAPIC with given vector
    static void route_irq(uint8_t irq, uint8_t vector, uint8_t dest_apic_id) noexcept;

    // Apply interrupt source overrides from MADT
    static void apply_overrides(const acpi::madt_info& info, uint8_t dest_apic_id) noexcept;

    static uint32_t read(uint8_t reg) noexcept;
    static void write(uint8_t reg, uint32_t value) noexcept;

private:
    static uintptr_t base_;
};

// High-level: initialize APIC subsystem from MADT info, disable legacy PIC
void apic_init(const acpi::madt_info& info) noexcept;

// Calibrate LAPIC timer using PIT and return initial count for ~1000 Hz
uint32_t calibrate_lapic_timer() noexcept;

} // namespace arch::amd64
