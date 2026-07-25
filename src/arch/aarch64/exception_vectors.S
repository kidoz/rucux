// SPDX-License-Identifier: MIT
//
// AArch64 exception vector table.
//
// VBAR_EL1 requires 2 KiB alignment; the table is 16 entries of 0x80 bytes,
// grouped in fours by originating state: EL1t, EL1h, EL0(64), EL0(32).

.section .vectors, "ax"

// Save the AAPCS caller-saved set plus x29/x30. Enough to call into C++ and
// return; callee-saved registers are preserved by the C++ side.
.macro SAVE_REGS
    sub     sp, sp, #256
    stp     x0,  x1,  [sp, #16 * 0]
    stp     x2,  x3,  [sp, #16 * 1]
    stp     x4,  x5,  [sp, #16 * 2]
    stp     x6,  x7,  [sp, #16 * 3]
    stp     x8,  x9,  [sp, #16 * 4]
    stp     x10, x11, [sp, #16 * 5]
    stp     x12, x13, [sp, #16 * 6]
    stp     x14, x15, [sp, #16 * 7]
    stp     x16, x17, [sp, #16 * 8]
    stp     x18, x19, [sp, #16 * 9]
    stp     x20, x21, [sp, #16 * 10]
    stp     x22, x23, [sp, #16 * 11]
    stp     x24, x25, [sp, #16 * 12]
    stp     x26, x27, [sp, #16 * 13]
    stp     x28, x29, [sp, #16 * 14]
    str     x30,      [sp, #16 * 15]
.endm

.macro RESTORE_REGS
    ldp     x0,  x1,  [sp, #16 * 0]
    ldp     x2,  x3,  [sp, #16 * 1]
    ldp     x4,  x5,  [sp, #16 * 2]
    ldp     x6,  x7,  [sp, #16 * 3]
    ldp     x8,  x9,  [sp, #16 * 4]
    ldp     x10, x11, [sp, #16 * 5]
    ldp     x12, x13, [sp, #16 * 6]
    ldp     x14, x15, [sp, #16 * 7]
    ldp     x16, x17, [sp, #16 * 8]
    ldp     x18, x19, [sp, #16 * 9]
    ldp     x20, x21, [sp, #16 * 10]
    ldp     x22, x23, [sp, #16 * 11]
    ldp     x24, x25, [sp, #16 * 12]
    ldp     x26, x27, [sp, #16 * 13]
    ldp     x28, x29, [sp, #16 * 14]
    ldr     x30,      [sp, #16 * 15]
    add     sp, sp, #256
.endm

// A fault we do not handle: report the class and halt.
.macro FAULT_ENTRY index
    .balign 0x80
    SAVE_REGS
    mov     x0, #\index
    mrs     x1, esr_el1
    mrs     x2, elr_el1
    bl      aarch64_fault_handler
    b       .
.endm

.macro IRQ_ENTRY
    .balign 0x80
    SAVE_REGS
    bl      aarch64_irq_handler
    RESTORE_REGS
    eret
.endm

.balign 2048
.global aarch64_vector_table
.type aarch64_vector_table, %object
aarch64_vector_table:
    // Current EL, SP_EL0
    FAULT_ENTRY 0                       // synchronous
    FAULT_ENTRY 1                       // IRQ
    FAULT_ENTRY 2                       // FIQ
    FAULT_ENTRY 3                       // SError

    // Current EL, SP_ELx — this is where the kernel itself runs
    FAULT_ENTRY 4                       // synchronous
    IRQ_ENTRY                           // IRQ
    FAULT_ENTRY 6                       // FIQ
    FAULT_ENTRY 7                       // SError

    // Lower EL, AArch64
    FAULT_ENTRY 8                       // synchronous
    FAULT_ENTRY 9                       // IRQ
    FAULT_ENTRY 10                      // FIQ
    FAULT_ENTRY 11                      // SError

    // Lower EL, AArch32
    FAULT_ENTRY 12                      // synchronous
    FAULT_ENTRY 13                      // IRQ
    FAULT_ENTRY 14                      // FIQ
    FAULT_ENTRY 15                      // SError
