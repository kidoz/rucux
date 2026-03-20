// SPDX-License-Identifier: MIT
// ARMv7 context switch and user-space transition.

.section .text

// ─── switch_context ────────────────────────────────────────────────────────
// void switch_context(uintptr_t* old_stack, uintptr_t new_stack);
//   r0 = pointer to old thread's stack_pointer (or NULL for first switch)
//   r1 = new thread's saved stack pointer
//
// Stack frame (9 words): r4, r5, r6, r7, r8, r9, r10, r11, lr(=pc)
// CPSR is not saved here — kernel threads always run in SVC mode.
// The CPSR is saved/restored by the exception entry/exit stubs.

.global switch_context
switch_context:
    cmp r0, #0
    beq .Lload_new

    // Save callee-saved registers + lr
    push {r4-r11, lr}

    // Save current stack pointer
    str sp, [r0]

.Lload_new:
    // Load new stack pointer
    mov sp, r1

    // Restore callee-saved registers; lr → pc (return to new thread)
    pop {r4-r11, pc}


// ─── jump_to_user_space ───────────────────────────────────────────────────
// void jump_to_user_space(void* entry, void* stack, void* arg);
//   r0 = user entry point
//   r1 = user stack pointer
//   r2 = argument (passed in r0 to user code)
//
// We use an exception return to drop to User mode:
//   1. Set SPSR_svc to User mode (0x10) with IRQs enabled
//   2. Set LR_svc to the user entry point
//   3. Set the user stack pointer (in User/System mode bank)
//   4. MOVS PC, LR — restores CPSR from SPSR and jumps

.global jump_to_user_space
jump_to_user_space:
    // Disable IRQs while we set up the exception return frame
    cpsid i

    // Save the argument — it needs to be in r0 when user code starts
    mov r3, r2          // r3 = arg (will be restored to r0 after mode switch)

    // Set SPSR to User mode (0x10), IRQ enabled (I=0), Thumb=0
    mov r2, #0x10
    msr spsr_cxsf, r2

    // Set user stack pointer: switch to System mode (shares regs with User),
    // set SP, then switch back to SVC mode.
    mrs r2, cpsr
    bic r2, r2, #0x1F
    orr r2, r2, #0x1F    // System mode = 0x1F
    msr cpsr_c, r2
    mov sp, r1            // Set user SP (System/User bank)

    // Switch back to SVC mode
    mrs r2, cpsr
    bic r2, r2, #0x1F
    orr r2, r2, #0x13    // SVC mode = 0x13
    msr cpsr_c, r2

    // Set return address to user entry
    mov lr, r0

    // Set r0 = arg for user code
    mov r0, r3

    // Exception return: restores CPSR from SPSR (→ User mode) and jumps to LR
    movs pc, lr
