// SPDX-License-Identifier: MIT
.section .text
.global _start

_start:
    // Clear argc and argv since we don't pass them yet
    xor %rdi, %rdi
    xor %rsi, %rsi

    // User stack and entry point are set by the kernel
    call main

    
    // Preserve main's status as the first argument to SYS_EXIT.
    mov %eax, %edi
    xor %rax, %rax
    syscall

1:  ud2
    jmp 1b
