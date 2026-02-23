// SPDX-License-Identifier: MIT
#include <kernel/print.hpp>
#include <stdint.h>

namespace arch::armv7 {

extern "C" void exception_handler() noexcept {
    kernel::print("Unhandled ARMv7 exception
");
    while (true) asm volatile("wfi");
}

// Exception Vector Table
// This table must be placed at 0x00000000 or some other VBAR offset.
extern "C" __attribute__((naked, section(".vectors"))) void vector_table() {
    asm volatile("ldr pc, [pc, #24]
                 " // Reset
                 "ldr pc, [pc, #24]
                 " // Undefined Instruction
                 "ldr pc, [pc, #24]
                 " // Software Interrupt (SWI)
                 "ldr pc, [pc, #24]
                 " // Prefetch Abort
                 "ldr pc, [pc, #24]
                 " // Data Abort
                 "ldr pc, [pc, #24]
                 " // Reserved
                 "ldr pc, [pc, #24]
                 " // IRQ
                 "ldr pc, [pc, #24]
                 " // FIQ
                 ".word exception_handler
                 "
                 ".word exception_handler
                 "
                 ".word exception_handler
                 "
                 ".word exception_handler
                 "
                 ".word exception_handler
                 "
                 ".word exception_handler
                 "
                 ".word exception_handler
                 "
                 ".word exception_handler
                 "
    );
}

void exceptions_init() noexcept {
    // Set Vector Base Address Register (VBAR) to point to our vector table
    asm volatile("mcr p15, 0, %0, c12, c0, 0" : : "r"(&vector_table));
}

} // namespace arch::armv7
