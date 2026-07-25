// SPDX-License-Identifier: MIT
#include <arch/amd64/apic.hpp>
#include <arch/amd64/idt.hpp>
#include <arch/amd64/io.hpp>
#include <arch/amd64/pic.hpp>
#include <kernel/print.hpp>

namespace arch::amd64 {

// ─── Local APIC ────────────────────────────────────────────────────────────

uintptr_t lapic::base_ = 0;

void lapic::init(uintptr_t base_address) noexcept {
    base_ = base_address;

    // Enable LAPIC via SVR (bit 8 = enable, vector 0xFF for spurious)
    write(lapic_reg::SVR, read(lapic_reg::SVR) | 0x1FF);

    // Clear Task Priority to accept all interrupts
    write(lapic_reg::TPR, 0);

    // Mask LINT0/LINT1 (we use I/O APIC for external interrupts)
    write(lapic_reg::LVT_LINT0, LAPIC_TIMER_MASKED);
    write(lapic_reg::LVT_LINT1, LAPIC_TIMER_MASKED);

    // Send EOI to clear any pending state
    send_eoi();
}

uint32_t lapic::read(uint32_t reg) noexcept {
    return *reinterpret_cast<volatile uint32_t*>(base_ + reg);
}

void lapic::write(uint32_t reg, uint32_t value) noexcept {
    *reinterpret_cast<volatile uint32_t*>(base_ + reg) = value;
}

void lapic::send_eoi() noexcept {
    write(lapic_reg::EOI, 0);
}

uint8_t lapic::id() noexcept {
    return static_cast<uint8_t>(read(lapic_reg::ID) >> 24);
}

void lapic::timer_init(uint8_t vector, uint32_t initial_count, uint8_t divide) noexcept {
    // Divide configuration: map values 1,2,4,8,16,32,64,128 to register encoding
    // Common: divide=3 → divide by 16
    write(lapic_reg::TIMER_DIV, divide & 0x0F);
    write(lapic_reg::LVT_TIMER, LAPIC_TIMER_PERIODIC | vector);
    write(lapic_reg::TIMER_INIT, initial_count);
}

void lapic::timer_stop() noexcept {
    write(lapic_reg::LVT_TIMER, LAPIC_TIMER_MASKED);
    write(lapic_reg::TIMER_INIT, 0);
}

void lapic::send_ipi(uint8_t target_apic_id, uint32_t icr_low) noexcept {
    write(lapic_reg::ICR_HIGH, static_cast<uint32_t>(target_apic_id) << 24);
    write(lapic_reg::ICR_LOW, icr_low);
    // Wait for delivery
    while (read(lapic_reg::ICR_LOW) & (1 << 12))
        ;
}

void lapic::send_init(uint8_t target_apic_id) noexcept {
    // INIT assert
    send_ipi(target_apic_id, ICR_INIT | ICR_LEVEL | ICR_ASSERT);
    // INIT deassert
    send_ipi(target_apic_id, ICR_INIT | ICR_LEVEL | ICR_DEASSERT);
}

void lapic::send_sipi(uint8_t target_apic_id, uint8_t vector) noexcept {
    send_ipi(target_apic_id, ICR_SIPI | vector);
}

// ─── I/O APIC ──────────────────────────────────────────────────────────────

uintptr_t ioapic::base_ = 0;

void ioapic::init(uintptr_t base_address) noexcept {
    base_ = base_address;
    kernel::print("IOAPIC: initialized at 0x{x}\n", reinterpret_cast<void*>(base_));
}

uint32_t ioapic::read(uint8_t reg) noexcept {
    *reinterpret_cast<volatile uint32_t*>(base_) = reg;
    return *reinterpret_cast<volatile uint32_t*>(base_ + 0x10);
}

void ioapic::write(uint8_t reg, uint32_t value) noexcept {
    *reinterpret_cast<volatile uint32_t*>(base_) = reg;
    *reinterpret_cast<volatile uint32_t*>(base_ + 0x10) = value;
}

void ioapic::route_irq(uint8_t irq, uint8_t vector, uint8_t dest_apic_id) noexcept {
    uint32_t redirect_low = vector; // Active high, edge triggered, fixed delivery
    uint32_t redirect_high = static_cast<uint32_t>(dest_apic_id) << 24;

    uint8_t reg_low = 0x10 + irq * 2;
    uint8_t reg_high = 0x10 + irq * 2 + 1;

    write(reg_high, redirect_high);
    write(reg_low, redirect_low);
}

void ioapic::apply_overrides(const acpi::madt_info& info, uint8_t dest_apic_id) noexcept {
    // First, route standard ISA IRQs (0-15) to vectors 0x20-0x2F
    for (uint8_t i = 0; i < 16; ++i) {
        route_irq(i, 0x20 + i, dest_apic_id);
    }

    // Apply interrupt source overrides from MADT
    for (uint32_t i = 0; i < info.override_count; ++i) {
        auto& iso = info.overrides[i];
        uint8_t gsi = static_cast<uint8_t>(iso.global_system_interrupt);
        uint8_t vector = 0x20 + iso.irq_source;

        uint32_t redirect_low = vector;

        // Apply polarity (bits 0-1 of flags)
        uint8_t polarity = iso.flags & 0x03;
        if (polarity == 3) // Active low
            redirect_low |= (1 << 13);

        // Apply trigger mode (bits 2-3 of flags)
        uint8_t trigger = (iso.flags >> 2) & 0x03;
        if (trigger == 3) // Level triggered
            redirect_low |= (1 << 15);

        uint32_t redirect_high = static_cast<uint32_t>(dest_apic_id) << 24;

        write(0x10 + gsi * 2 + 1, redirect_high);
        write(0x10 + gsi * 2, redirect_low);
    }
}

// ─── High-level init ───────────────────────────────────────────────────────

static void disable_legacy_pic() noexcept {
    // Mask all IRQs on both PICs
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

void apic_init(const acpi::madt_info& info) noexcept {
    disable_legacy_pic();
    idt_set_apic_mode(true);

    lapic::init(static_cast<uintptr_t>(info.lapic_address));
    kernel::print("LAPIC: initialized, ID={}\n", lapic::id());

    if (info.io_apic_address) {
        ioapic::init(static_cast<uintptr_t>(info.io_apic_address));
        ioapic::apply_overrides(info, lapic::id());
    }
}

uint32_t calibrate_lapic_timer() noexcept {
    // Use PIT channel 2 to calibrate LAPIC timer.
    // Set PIT to one-shot mode, count down from a known value,
    // measure how many LAPIC ticks pass in ~10ms.

    // PIT frequency = 1193182 Hz
    // 10ms = 11932 PIT ticks
    constexpr uint16_t pit_count = 11932;

    // Set LAPIC timer divide to 16
    lapic::write(lapic_reg::TIMER_DIV, 0x03);
    // Set initial count to max
    lapic::write(lapic_reg::TIMER_INIT, 0xFFFFFFFF);

    // Program PIT channel 2 for one-shot
    outb(0x61, (inb(0x61) & 0xFD) | 1); // Enable speaker gate, disable speaker output
    outb(0x43, 0xB2);                   // Channel 2, lobyte/hibyte, one-shot
    outb(0x42, pit_count & 0xFF);
    outb(0x42, (pit_count >> 8) & 0xFF);

    // Reset PIT counter
    uint8_t tmp = inb(0x61) & 0xFE;
    outb(0x61, tmp);
    outb(0x61, tmp | 1);

    // Wait for PIT to count down (bit 5 of port 0x61 goes high)
    while (!(inb(0x61) & 0x20))
        ;

    // Read how many LAPIC ticks passed
    uint32_t elapsed = 0xFFFFFFFF - lapic::read(lapic_reg::TIMER_CURR);
    lapic::write(lapic_reg::TIMER_INIT, 0); // Stop

    // Scale to 1000 Hz (10ms × 100 = 1s)
    uint32_t ticks_per_sec = elapsed * 100;
    uint32_t initial_count = ticks_per_sec / 1000; // For 1000 Hz

    kernel::print("LAPIC timer: {} ticks/10ms, initial_count={} for 1kHz\n", elapsed, initial_count);
    return initial_count;
}

} // namespace arch::amd64
