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

    // Stash user rsp in this CPU's per_cpu.temp_user_rsp slot (offset 40),
    // load this CPU's kernel stack from per_cpu.kernel_stack (offset 32),
    // then push the saved user rsp onto the freshly-loaded kernel stack.
    // Both slots live in %gs per_cpu so this is SMP-safe and re-entrant.
    mov %rsp, %gs:40
    mov %gs:32, %rsp
    pushq %gs:40

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

    // Save rax (return value) before calling check_signals
    push %rax

    // Pass pointer to saved registers
    // Current stack:
    // rsp+0:  rax (return value)
    // rsp+8:  arg7
    // rsp+16: r15
    // rsp+24: r14
    // rsp+32: r13
    // rsp+40: r12
    // rsp+48: rbx
    // rsp+56: rbp
    // rsp+64: r11 (RFLAGS)
    // rsp+72: rcx (RIP)
    // rsp+80: user rsp
    mov %rsp, %rdi
    .extern check_signals
    call check_signals

    pop %rax
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

.global irq4_entry
irq4_entry:
    push %rax
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11

    .extern irq4_handler
    call irq4_handler

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

// ─── Exception stubs (no error code) ───────────────────────────────────────
// For vectors 0-7, 9, 16-19 (no hardware error code pushed)
.macro ISR_NOERR vec
.global isr_stub_\vec
isr_stub_\vec:
    push $0           // dummy error code
    push %rax
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
    mov $\vec, %rdi
    mov 80(%rsp), %rsi // error code (dummy 0)
    mov 88(%rsp), %rdx // rip
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
    add $8, %rsp      // pop error code
    iretq
.endm

// For vectors 8, 10-14, 17 (hardware pushes error code)
.macro ISR_ERR vec
.global isr_stub_\vec
isr_stub_\vec:
    push %rax
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
    mov $\vec, %rdi
    mov 72(%rsp), %rsi // error code
    mov 80(%rsp), %rdx // rip
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
    add $8, %rsp      // pop error code
    iretq
.endm

.extern isr_handler

ISR_NOERR 0   // Divide by zero
ISR_NOERR 1   // Debug
ISR_NOERR 2   // NMI
ISR_NOERR 3   // Breakpoint
ISR_NOERR 4   // Overflow
ISR_NOERR 5   // Bound range
ISR_NOERR 6   // Invalid opcode
ISR_NOERR 7   // Device not available
ISR_ERR   8   // Double fault
ISR_NOERR 9   // Coprocessor segment
ISR_ERR   10  // Invalid TSS
ISR_ERR   11  // Segment not present
ISR_ERR   12  // Stack-segment fault
ISR_ERR   13  // General protection fault
ISR_ERR   14  // Page fault
ISR_NOERR 16  // x87 FPE
ISR_ERR   17  // Alignment check
ISR_NOERR 18  // Machine check
ISR_NOERR 19  // SIMD FPE

