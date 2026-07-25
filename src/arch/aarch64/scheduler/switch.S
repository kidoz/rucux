// SPDX-License-Identifier: MIT
// AArch64 context switch and user-space transition.

.section .text

// ─── switch_context ────────────────────────────────────────────────────────
// void switch_context(uintptr_t* old_stack, uintptr_t new_stack);
//   x0 = &old_thread->stack_pointer, or NULL on the first switch
//   x1 = new thread's saved stack pointer
//
// Saves the AAPCS64 callee-saved set: x19-x28, x29 (FP), x30 (LR).
// 12 registers = 96 bytes, which keeps SP 16-byte aligned as the ABI requires.
// PSTATE is not saved: kernel threads always run at EL1h, and exception
// entry/exit handles state for threads interrupted from EL0.
//
// The frame layout must match the initial stack built in scheduler.cpp, where
// slot 11 (offset 88) holds the entry point that `ret` jumps to.

.global switch_context
.type switch_context, %function
switch_context:
    cbz     x0, .Lload_new

    sub     sp, sp, #96
    stp     x19, x20, [sp, #16 * 0]
    stp     x21, x22, [sp, #16 * 1]
    stp     x23, x24, [sp, #16 * 2]
    stp     x25, x26, [sp, #16 * 3]
    stp     x27, x28, [sp, #16 * 4]
    stp     x29, x30, [sp, #16 * 5]

    mov     x2, sp
    str     x2, [x0]

.Lload_new:
    mov     sp, x1
    ldp     x19, x20, [sp, #16 * 0]
    ldp     x21, x22, [sp, #16 * 1]
    ldp     x23, x24, [sp, #16 * 2]
    ldp     x25, x26, [sp, #16 * 3]
    ldp     x27, x28, [sp, #16 * 4]
    ldp     x29, x30, [sp, #16 * 5]
    add     sp, sp, #96
    ret

// ─── jump_to_user_space ───────────────────────────────────────────────────
// void jump_to_user_space(void* entry, void* stack, void* arg);
//   x0 = user entry point
//   x1 = user stack pointer
//   x2 = argument, delivered in x0 to user code
//
// AArch64 has no mode banking to unwind: dropping to EL0 is an exception
// return with SP_EL0, ELR_EL1, and SPSR_EL1 set.

.global jump_to_user_space
.type jump_to_user_space, %function
jump_to_user_space:
    msr     daifset, #2         // mask IRQ while building the return frame
    msr     sp_el0, x1
    msr     elr_el1, x0
    mov     x3, xzr             // SPSR: EL0t, DAIF clear so userspace runs with IRQs on
    msr     spsr_el1, x3
    mov     x0, x2
    eret
