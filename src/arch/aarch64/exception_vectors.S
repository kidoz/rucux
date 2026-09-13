// SPDX-License-Identifier: MIT
//
// AArch64 exception vector table.
//
// VBAR_EL1 requires 2 KiB alignment; the table is 16 entries of exactly 0x80
// bytes, grouped in fours by originating state: EL1t, EL1h, EL0(64), EL0(32).
//
// Each slot holds only a branch. Register save/restore does not fit in 0x80
// bytes — SAVE_REGS plus RESTORE_REGS alone is ~136 bytes — and an oversized
// entry silently shifts every following slot to the next 0x80 boundary, so the
// handler bodies live out of line in .text.

// Preserve all GPRs and the exception return state across C++ calls and
// scheduling. Each suspended exception keeps its own ELR, SPSR and user SP.
.macro SAVE_REGS
    sub     sp, sp, #288
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
    mrs     x0, elr_el1
    mrs     x1, spsr_el1
    stp     x0, x1, [sp, #256]
    mrs     x0, sp_el0
    str     x0, [sp, #272]
.endm

.macro RESTORE_REGS
    ldp     x0, x1, [sp, #256]
    msr     elr_el1, x0
    msr     spsr_el1, x1
    ldr     x0, [sp, #272]
    msr     sp_el0, x0
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
    add     sp, sp, #288
.endm

// ─── Vector table ─────────────────────────────────────────────────────────

.macro VENTRY label
    .balign 0x80
    b       \label
.endm

.section .vectors, "ax"
.balign 2048
.global aarch64_vector_table
.type aarch64_vector_table, %object
aarch64_vector_table:
    // Current EL, SP_EL0
    VENTRY  vec_el1t_sync
    VENTRY  vec_el1t_irq
    VENTRY  vec_el1t_fiq
    VENTRY  vec_el1t_serror

    // Current EL, SP_ELx — where the kernel itself runs
    VENTRY  vec_el1h_sync
    VENTRY  vec_el1h_irq
    VENTRY  vec_el1h_fiq
    VENTRY  vec_el1h_serror

    // Lower EL, AArch64 — userspace
    VENTRY  vec_el0_sync
    VENTRY  vec_el0_irq
    VENTRY  vec_el0_fiq
    VENTRY  vec_el0_serror

    // Lower EL, AArch32 — unused; the kernel never drops to AArch32
    VENTRY  vec_el0_32_sync
    VENTRY  vec_el0_32_irq
    VENTRY  vec_el0_32_fiq
    VENTRY  vec_el0_32_serror

// ─── Handler bodies ───────────────────────────────────────────────────────

.section .text

// A fault we do not handle: report the vector index and halt.
.macro FAULT_STUB name, index
\name:
    SAVE_REGS
    mov     x0, #\index
    mrs     x1, esr_el1
    mrs     x2, elr_el1
    bl      aarch64_fault_handler
    b       .
.endm

FAULT_STUB vec_el1t_sync,    0
FAULT_STUB vec_el1t_irq,     1
FAULT_STUB vec_el1t_fiq,     2
FAULT_STUB vec_el1t_serror,  3
FAULT_STUB vec_el1h_sync,    4
FAULT_STUB vec_el1h_fiq,     6
FAULT_STUB vec_el1h_serror,  7
FAULT_STUB vec_el0_fiq,      10
FAULT_STUB vec_el0_serror,   11
FAULT_STUB vec_el0_32_sync,  12
FAULT_STUB vec_el0_32_irq,   13
FAULT_STUB vec_el0_32_fiq,   14
FAULT_STUB vec_el0_32_serror, 15

// IRQ taken while the kernel was running, and IRQ taken while userspace was
// running. Userspace runs with DAIF clear, so the timer tick lands here
// constantly — routing it to a fault stub halts the moment EL0 starts.
vec_el1h_irq:
vec_el0_irq:
    SAVE_REGS
    bl      aarch64_irq_handler
    RESTORE_REGS
    eret

// Synchronous exception from userspace: an SVC syscall, or a fault taken at
// EL0. The C++ side reads ESR_EL1 to tell them apart and may rewrite the saved
// x0 with a syscall return value, so the frame pointer is passed in.
vec_el0_sync:
    SAVE_REGS
    mov     x0, sp
    bl      aarch64_lower_sync_handler
    RESTORE_REGS
    eret
