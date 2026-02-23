// SPDX-License-Identifier: MIT

.section .multiboot_header
.align 4
.long 0x1BADB002               /* Multiboot magic */
.long 0x00000007               /* Flags: ALIGN | MEMINFO | VIDEO */
.long -(0x1BADB002 + 0x00000007) /* Checksum */

.long 0 /* header_addr */
.long 0 /* load_addr */
.long 0 /* load_end_addr */
.long 0 /* bss_end_addr */
.long 0 /* entry_addr */

.long 0    /* mode_type (0 = linear graphics) */
.long 1024 /* width */
.long 768  /* height */
.long 32   /* depth */

.section .bss
.align 16
stack_bottom:
.skip 16384 /* 16 KiB */
stack_top:

/* Initial Page Tables for 64-bit transition */
.align 4096
p4_table:
    .skip 4096
p3_table:
    .skip 4096
p2_table:
    .skip 4096

.section .text
.code32
.global _start32
_start32:
    mov $stack_top, %esp
    
    /* Save Multiboot info (ebx) and magic (eax) */
    push $0
    push %ebx
    push $0
    push %eax

    /* Set up Page Tables */
    /* Clear page tables first */
    mov $p4_table, %edi
    xor %eax, %eax
    mov $3072, %ecx /* 3 tables * 1024 dwords = 3072 */
    rep stosl

    /* Map first 1GB to p2_table[0..511] using huge pages */
    mov $p3_table, %eax
    or $0x03, %eax      /* Present + Writable */
    mov %eax, p4_table

    mov $p2_table, %eax
    or $0x03, %eax
    mov %eax, p3_table

    /* Identity map 0-1GB */
    mov $0, %ecx
.map_loop:
    mov $0x200000, %eax
    mul %ecx
    or $0x00000083, %eax /* Present + Writable + Huge */
    mov %eax, p2_table(,%ecx,8)
    inc %ecx
    cmp $512, %ecx
    jne .map_loop

    /* Load P4 to CR3 */
    mov $p4_table, %eax
    mov %eax, %cr3

    /* Enable PAE */
    mov %cr4, %eax
    or $0x20, %eax
    mov %eax, %cr4

    /* Enable Long Mode in EFER MSR */
    mov $0xC0000080, %ecx
    rdmsr
    or $0x100, %eax
    wrmsr

    /* Enable Paging */
    mov %cr0, %eax
    or $0x80000000, %eax
    mov %eax, %cr0

    /* Load 64-bit GDT */
    lgdt gdt64_pointer

    /* Jump to 64-bit code */
    ljmp $0x08, $_start64

.code64
_start64:
    /* Set up data segments */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    /* Get Multiboot info back to registers */
    pop %rdi /* magic */
    pop %rsi /* info ptr (was in ebx) */

    /* Call C++ entry point */
    .extern kernel_main
    call kernel_main

    hlt
.loop:
    jmp .loop

.section .data
.align 8
gdt64:
    .quad 0 /* Null Descriptor */
    .quad 0x00209A0000000000 /* 64-bit Code Descriptor (RX) */
    .quad 0x0000920000000000 /* 64-bit Data Descriptor (RW) */
gdt64_pointer:
    .word gdt64_pointer - gdt64 - 1
    .quad gdt64
