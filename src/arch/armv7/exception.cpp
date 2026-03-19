// SPDX-License-Identifier: MIT
#include <arch/armv7/gic.hpp>
#include <arch/armv7/timer.hpp>
#include <kernel/cpu/percpu.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <stdint.h>

namespace arch::armv7 {

// Virtual timer IRQ ID on most ARMv7 platforms (PPI 27 = GIC IRQ 27)
static constexpr uint32_t TIMER_VIRQ = 27;

extern "C" void exception_handler() noexcept {
    kernel::print("Unhandled ARMv7 exception\n");
    while (true) asm volatile("wfi");
}

extern "C" void irq_handler_arm() noexcept {
    uint32_t irq_id = gic_cpu_interface::acknowledge();
    uint32_t irq_num = irq_id & 0x3FF; // Bits [9:0] = interrupt ID

    if (irq_num == 1023) {
        // Spurious interrupt — no pending IRQ
        return;
    }

    if (irq_num == TIMER_VIRQ) {
        // Timer interrupt: reload timer and schedule
        kernel::cpu::this_cpu()->ticks++;
        generic_timer::set_timer(generic_timer::get_frequency() / 1000);
        kernel::scheduler::scheduler::schedule();
    } else {
        // Other interrupts — wake any waiters
        kernel::scheduler::scheduler::wake_irq_waiters(static_cast<uint8_t>(irq_num));
    }

    gic_cpu_interface::end_of_interrupt(irq_id);
}

// Exception Vector Table
// Each vector loads a handler address from the table that follows.
// IRQ vector (offset 0x18) now points to irq_handler_arm.
extern "C" __attribute__((naked, section(".vectors"))) void vector_table() {
    asm volatile(
        "ldr pc, [pc, #24]\n"  // 0x00: Reset
        "ldr pc, [pc, #24]\n"  // 0x04: Undefined Instruction
        "ldr pc, [pc, #24]\n"  // 0x08: SVC (Software Interrupt)
        "ldr pc, [pc, #24]\n"  // 0x0C: Prefetch Abort
        "ldr pc, [pc, #24]\n"  // 0x10: Data Abort
        "ldr pc, [pc, #24]\n"  // 0x14: Reserved
        "ldr pc, [pc, #24]\n"  // 0x18: IRQ
        "ldr pc, [pc, #24]\n"  // 0x1C: FIQ
        ".word exception_handler\n"   // Reset
        ".word exception_handler\n"   // Undefined
        ".word exception_handler\n"   // SVC
        ".word exception_handler\n"   // Prefetch Abort
        ".word exception_handler\n"   // Data Abort
        ".word exception_handler\n"   // Reserved
        ".word irq_handler_arm\n"     // IRQ → GIC handler
        ".word exception_handler\n"   // FIQ
    );
}

void exceptions_init() noexcept {
    // Set Vector Base Address Register (VBAR) to our vector table
    asm volatile("mcr p15, 0, %0, c12, c0, 0" :: "r"(&vector_table));
}

} // namespace arch::armv7
