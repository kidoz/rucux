// SPDX-License-Identifier: MIT

.section .text.boot, "ax"
.global _start
.type _start, %function

_start:
    .rept 8
    mov r0, r0
    .endr
    b _start_real
    .word 0x016f2818
    .word _start
    .word kernel_end
    .word 0

_start_real:
    mov r4, r0
    mov r5, r1
    mov r6, r2

    ldr r1, =__bss_start
    ldr r2, =__bss_end
    mov r3, #0

1:
    cmp r1, r2
    bhs 2f
    str r3, [r1], #4
    b 1b

2:
    ldr sp, =boot_stack_top
    mov r0, r4
    mov r1, r5
    mov r2, r6
    bl kernel_main

3:
    b 3b

.section .bss.boot_stack, "aw", %nobits
.balign 16
boot_stack:
    .space 16384
boot_stack_top:
