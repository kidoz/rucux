// SPDX-License-Identifier: MIT
.section .text
.global _start

_start:
    // Clear argc and argv since we don't pass them yet
    xor %rdi, %rdi
    xor %rsi, %rsi

    // User stack and entry point are set by the kernel
    call main

    
    // Syscall exit (rax = 0)
    xor %rdi, %rdi
    xor %rax, %rax
    syscall
