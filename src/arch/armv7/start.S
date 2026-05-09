// SPDX-License-Identifier: MIT

.section .text.boot, "ax"
.global _start
.type _start, %function

_start:
    mov r4, r0

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
    bl kernel_main

3:
    b 3b

.section .bss.boot_stack, "aw", %nobits
.balign 16
boot_stack:
    .space 16384
boot_stack_top:
