// SPDX-License-Identifier: MIT
.section .text
.global switch_context

switch_context:
    // r0 = uintptr_t* old_stack
    // r1 = uintptr_t new_stack
    
    // If old_stack is null, we are starting the first thread
    cmp r0, #0
    beq .load_new
    
    // Save callee-saved registers (r4-r11, lr)
    push {r4-r11, lr}
    
    // Save current stack pointer
    str sp, [r0]

.load_new:
    // Load new stack pointer
    mov sp, r1
    
    // Restore callee-saved registers
    pop {r4-r11, pc}

.global jump_to_user_space
jump_to_user_space:
    // r0 = user entry point
    // r1 = user stack
    
    // Switch to User mode (0x10) with CPSR
    // Using an 'iret'-like sequence for ARM: subs pc, lr, #0
    // But commonly done via movs pc, lr or using an exception return.
    
    mov r13, r1     // Set user stack (if in System/User mode)
    mov r14, r0     // Set return address
    
    // This is simplified. Proper transition involves SPSR and an exception return.
    msr cpsr_c, #0x10
    mov pc, r14
