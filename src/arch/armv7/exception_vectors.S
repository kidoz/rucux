// SPDX-License-Identifier: MIT
// ARMv7 Exception Vector Table and entry stubs.
// Each vector saves state and calls a C++ handler.

.section .vectors, "ax"
.global vector_table
.balign 32

vector_table:
    b   reset_entry         // 0x00: Reset
    b   undef_entry         // 0x04: Undefined Instruction
    b   svc_entry           // 0x08: Supervisor Call (SWI)
    b   prefetch_entry      // 0x0C: Prefetch Abort
    b   data_abort_entry    // 0x10: Data Abort
    b   .                   // 0x14: Reserved
    b   irq_entry           // 0x18: IRQ
    b   fiq_entry           // 0x1C: FIQ

// ─── Reset ─────────────────────────────────────────────────────────────────
.section .text
reset_entry:
    // Should not happen at runtime
    b   .

// ─── Undefined Instruction ─────────────────────────────────────────────────
undef_entry:
    // Save user registers on SVC stack
    stmfd sp!, {r0-r12, lr}
    bl  undefined_handler
    ldmfd sp!, {r0-r12, pc}^   // ^ = restore CPSR from SPSR

// ─── SVC (Syscall) ────────────────────────────────────────────────────────
// On SVC entry: LR_svc = instruction after SVC, SPSR_svc = user CPSR
// Syscall number is in r7 (Linux ARM EABI convention) or r0 (our convention).
// We use: num=r0, a1=r1, a2=r2, a3=r3, a4=r4, a5=r5, a6=r6
// Return value goes in r0.
svc_entry:
    // Save user state
    stmfd sp!, {r0-r12, lr}

    // Extract syscall number from the SVC instruction word
    // LR_svc points to instruction AFTER the SVC, so SVC is at LR-4
    // SVC immediate is in bits [23:0] of the instruction
    // However, our userspace passes syscall number in r0, so just use that.

    // Call C++ handler: svc_handler(r0=num, r1=a1, r2=a2, r3=a3, r4=a4, r5=a5, r6=a6)
    // r0-r3 are already in the right registers
    // r4-r6 need to be loaded from the saved stack frame
    ldr r4, [sp, #16]    // saved r4
    ldr r5, [sp, #20]    // saved r5
    ldr r6, [sp, #24]    // saved r6

    // Push extra args on stack for C++ call (ARM calling convention: first 4 in r0-r3, rest on stack)
    stmfd sp!, {r4-r6}
    bl  svc_handler
    add sp, sp, #12       // Clean up stack args

    // Store return value (r0) into the saved r0 slot so it's returned to user
    str r0, [sp, #0]

    // Restore user state and return
    ldmfd sp!, {r0-r12, pc}^

// ─── Prefetch Abort ────────────────────────────────────────────────────────
prefetch_entry:
    sub lr, lr, #4        // LR points to faulting instruction + 4
    stmfd sp!, {r0-r12, lr}

    // r0 = fault address (IFAR)
    mrc p15, 0, r0, c6, c0, 2  // IFAR
    // r1 = IFSR
    mrc p15, 0, r1, c5, c0, 1  // IFSR

    bl  prefetch_abort_handler

    ldmfd sp!, {r0-r12, pc}^

// ─── Data Abort ────────────────────────────────────────────────────────────
data_abort_entry:
    sub lr, lr, #8        // Data abort: LR = faulting instruction + 8
    stmfd sp!, {r0-r12, lr}

    // r0 = fault address (DFAR)
    mrc p15, 0, r0, c6, c0, 0  // DFAR
    // r1 = DFSR
    mrc p15, 0, r1, c5, c0, 0  // DFSR

    bl  data_abort_handler

    ldmfd sp!, {r0-r12, pc}^

// ─── IRQ ───────────────────────────────────────────────────────────────────
irq_entry:
    sub lr, lr, #4        // IRQ: LR = next instruction + 4
    stmfd sp!, {r0-r12, lr}

    bl  irq_handler_arm

    ldmfd sp!, {r0-r12, pc}^

// ─── FIQ ───────────────────────────────────────────────────────────────────
fiq_entry:
    sub lr, lr, #4
    stmfd sp!, {r0-r7, lr}

    bl  fiq_handler

    ldmfd sp!, {r0-r7, pc}^
