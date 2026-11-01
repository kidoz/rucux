// SPDX-License-Identifier: MIT
.arch armv7-a
.syntax unified
.arm
.section .text
.global armv7_ap_entry
.type armv7_ap_entry, %function
armv7_ap_entry:
    cpsid if
    // PSCI context is the logical CPU index; never re-zero shared BSS here.
    cmp r0, #8
    bhs 1f
    ldr r1, =armv7_ap_stacks
    add r1, r1, r0, lsl #14
    add sp, r1, #16384
    mov fp, #0
    bl ap_entry_arm
1:  wfi
    b 1b

.global armv7_exception_stacks
.type armv7_exception_stacks, %function
armv7_exception_stacks:
    mrs r2, cpsr
    mov r3, lr
    cpsid if
    ldr r1, =exception_stacks
    add r1, r1, r0, lsl #14
    cps #0x12
    add r1, r1, #4096
    mov sp, r1
    cps #0x17
    add r1, r1, #4096
    mov sp, r1
    cps #0x1b
    add r1, r1, #4096
    mov sp, r1
    cps #0x11
    add r1, r1, #4096
    mov sp, r1
    msr cpsr_c, r2
    bx r3

.section .bss, "aw", %nobits
.balign 16
armv7_ap_stacks:
    .space 8 * 16384
exception_stacks:
    .space 8 * 16384
