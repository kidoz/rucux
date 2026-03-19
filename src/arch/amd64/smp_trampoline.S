// SPDX-License-Identifier: MIT
// AP (Application Processor) startup trampoline.
// This code is copied to physical address 0x8000 (below 1MB) before SIPI.
// APs start in 16-bit real mode and must transition to long mode.

.code16
.section .text
.global ap_trampoline_start
.global ap_trampoline_end

ap_trampoline_start:
    cli

    // Load the GDT pointer placed by BSP at a fixed offset after the trampoline
    lgdt ap_gdt_ptr - ap_trampoline_start + 0x8000

    // Enable protected mode (PE bit in CR0)
    mov %cr0, %eax
    or $1, %eax
    mov %eax, %cr0

    // Far jump to 32-bit code
    ljmp $0x08, $(ap_trampoline32 - ap_trampoline_start + 0x8000)

.code32
ap_trampoline32:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss

    // Enable PAE (CR4.PAE = bit 5)
    mov %cr4, %eax
    or $(1 << 5), %eax
    mov %eax, %cr4

    // Load PML4 address from BSP-provided variable
    mov (ap_pml4 - ap_trampoline_start + 0x8000), %eax
    mov %eax, %cr3

    // Enable long mode via EFER MSR (bit 8 = LME)
    mov $0xC0000080, %ecx
    rdmsr
    or $(1 << 8), %eax
    wrmsr

    // Enable paging (CR0.PG = bit 31)
    mov %cr0, %eax
    or $(1 << 31), %eax
    mov %eax, %cr0

    // Far jump to 64-bit code
    ljmp $0x08, $(ap_trampoline64 - ap_trampoline_start + 0x8000)

.code64
ap_trampoline64:
    // Load 64-bit GDT (same as BSP)
    lgdt (ap_gdt64_ptr - ap_trampoline_start + 0x8000)

    // Reload segments
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    xor %ax, %ax
    mov %ax, %fs
    mov %ax, %gs

    // Load stack pointer from BSP-provided variable
    mov (ap_stack - ap_trampoline_start + 0x8000), %rsp

    // Jump to C++ AP entry
    mov (ap_entry - ap_trampoline_start + 0x8000), %rax
    call *%rax

    // Should not return
1:  hlt
    jmp 1b

// ─── Data placed by BSP ────────────────────────────────────────────────────
.align 4
.global ap_pml4
.global ap_stack
.global ap_entry
ap_pml4:     .long 0       // Filled by BSP: physical address of PML4
ap_stack:    .quad 0       // Filled by BSP: AP kernel stack top
ap_entry:    .quad 0       // Filled by BSP: C++ entry point (ap_main)
ap_gdt64_ptr:.word 0       // Filled by BSP: 64-bit GDT pointer (limit)
             .quad 0       //                 (base)

// Temporary GDT for real→protected→long mode transition
.align 16
ap_gdt:
    .quad 0                 // Null
    .quad 0x00CF9A000000FFFF // Code32: base=0, limit=4G, DPL=0, exec/read
    .quad 0x00CF92000000FFFF // Data32: base=0, limit=4G, DPL=0, read/write
ap_gdt_ptr:
    .word ap_gdt_ptr - ap_gdt - 1
    .long ap_gdt - ap_trampoline_start + 0x8000

ap_trampoline_end:
