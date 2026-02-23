// SPDX-License-Identifier: MIT
.section .text
.global switch_context

switch_context:
    // rdi = uintptr_t* old_stack
    // rsi = uintptr_t new_stack
    
    // If old_stack is null, we are starting the first thread
    test %rdi, %rdi
    jz .load_new
    
    // Save callee-saved registers
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    
    // Save current stack pointer
    mov %rsp, (%rdi)

.load_new:
    // Load new stack pointer
    mov %rsi, %rsp
    
    // Restore callee-saved registers
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    
    ret

.global syscall_entry
syscall_entry:
    swapgs
    
    // Save user rsp in a scratch register that is NOT r11 or rcx.
    // r12 is callee-saved, but we haven't pushed it yet. We can just use it,
    // and then push it. Wait, if we overwrite r12, we lose the user's r12!
    // We must push the user's registers, but we are on the user stack!
    // NO! We can't push to the user stack, it might not be valid or mapped properly if we switch CR3!
    // Wait, the user stack IS mapped, because we haven't switched CR3 yet.
    // BUT we shouldn't push to it.
    // Is there a scratch register we can use? 
    // We can just push the user stack pointer to the KERNEL stack after switching!
    // But how to switch without a scratch register?
    // We can exchange! xchg %rsp, g_current_kernel_stack(%rip). But that's memory-to-register xchg, which isn't allowed without a register.
    // We can use the kernel stack directly:
    // mov %rsp, %gs:scratch_space? We don't have GS base set up.
    
    // Let's just push %rsp to the user stack temporarily, load the new stack,
    // pop the old %rsp from the user stack into a register, and push it to the kernel stack!
    push %rsp
    mov g_current_kernel_stack(%rip), %rsp
    // Now rsp = kernel stack.
    // The user stack has the old rsp at the top.
    // We need to read it. But we don't know the user stack pointer anymore!
    
    // Let's use a global variable to temporarily hold the user rsp.
    // We are not SMP yet, so this is safe.
    .extern g_temp_user_rsp
    mov %rsp, g_temp_user_rsp(%rip)
    .extern g_current_kernel_stack
    mov g_current_kernel_stack(%rip), %rsp
    push g_temp_user_rsp(%rip)
    
    // Save user registers
    push %rcx // RIP
    push %r11 // RFLAGS
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    
    // Syscall ABI: num=rax, a1=rdi, a2=rsi, a3=rdx, a4=r10, a5=r8, a6=r9
    // C++ ABI: arg1=rdi, arg2=rsi, arg3=rdx, arg4=rcx, arg5=r8, arg6=r9, arg7=stack
    
    push %r9       // arg7 (a6)
    mov %r8, %r9   // arg6 (a5)
    mov %r10, %r8  // arg5 (a4)
    mov %rdx, %rcx // arg4 (a3)
    mov %rsi, %rdx // arg3 (a2)
    mov %rdi, %rsi // arg2 (a1)
    mov %rax, %rdi // arg1 (num)
    
    call syscall_dispatch
    add $8, %rsp   // Cleanup arg7

    // Restore user registers
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    pop %r11 // RFLAGS
    pop %rcx // RIP
    
    // Restore user stack pointer
    pop %rsp
    
    swapgs
    sysretq

.global jump_to_user_space
jump_to_user_space:
    // rdi = user entry point
    // rsi = user stack
    // rdx = user arg
    
    // Prepare iretq stack frame
    // Ring 3 selectors: CS = 0x23 (4 * 8 | 3), SS = 0x1B (3 * 8 | 3)
    mov $0x1B, %ax
    mov %ax, %ds
    mov %ax, %es
    
    pushq $0x1B      // SS
    pushq %rsi       // RSP
    pushq $0x202     // RFLAGS (IF enabled)
    pushq $0x23      // CS
    pushq %rdi       // RIP
    
    mov %rdx, %rdi   // Set user argument 1
    
    iretq

.global irq0_entry
irq0_entry:
    push %rax
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
    
    .extern irq0_handler
    call irq0_handler

    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rax
    iretq

.global irq1_entry
irq1_entry:
    push %rax
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
    
    .extern irq1_handler
    call irq1_handler

    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rax
    iretq

.global isr_stub_14
isr_stub_14:
    push %rax
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
    
    mov $14, %rdi
    mov 72(%rsp), %rsi // Get Error Code into rsi (arg2)
    .extern isr_handler
    call isr_handler
    
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rax
    add $8, %rsp // pop error code
    iretq

