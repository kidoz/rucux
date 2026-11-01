// SPDX-License-Identifier: MIT
// Firmware reserves 0x8000 before ExitBootServices. No stack is used until the
// per-AP 64-bit stack is installed; all copied-code references are relocated.
.section .text
.code16
.global ap_trampoline_start, ap_trampoline_end
ap_trampoline_start:
    cli
    cld
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    lgdt (ap_gdt_ptr - ap_trampoline_start + 0x8000)
    mov %cr0, %eax
    or $1, %eax
    mov %eax, %cr0
    ljmp $0x08, $(ap_trampoline32 - ap_trampoline_start + 0x8000)
.code32
ap_trampoline32:
    mov $0x18, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    // PAE and baseline FXSAVE/SSE support, matching the scheduler's FPU ABI.
    mov %cr4, %eax
    or $0x620, %eax
    mov %eax, %cr4
    mov (ap_pml4 - ap_trampoline_start + 0x8000), %eax
    mov %eax, %cr3
    mov $0xC0000080, %ecx
    rdmsr
    or $0x900, %eax // LME + NXE: the shared tables may contain NX leaves.
    wrmsr
    mov %cr0, %eax
    and $~0x0c, %eax // Clear EM/TS before the first FXSAVE/FXRSTOR.
    or $0x80010002, %eax // Paging, write protection, monitor coprocessor.
    mov %eax, %cr0
    ljmp $0x10, $(ap_trampoline64 - ap_trampoline_start + 0x8000)
.code64
ap_trampoline64:
    mov $0x18, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    xor %ax, %ax
    mov %ax, %fs
    mov %ax, %gs
    mov (ap_stack - ap_trampoline_start + 0x8000), %rsp
    xor %ebp, %ebp
    mov (ap_cpu - ap_trampoline_start + 0x8000), %edi
    mov (ap_entry - ap_trampoline_start + 0x8000), %rax
    call *%rax
1:  cli
    hlt
    jmp 1b
.balign 8
.global ap_pml4, ap_stack, ap_entry, ap_cpu
ap_pml4: .long 0
ap_cpu: .long 0
ap_stack: .quad 0
ap_entry: .quad 0
.balign 8
ap_gdt:
    .quad 0
    .quad 0x00CF9A000000FFFF // 0x08: protected-mode code
    .quad 0x00AF9A000000FFFF // 0x10: long-mode code (L=1, D=0)
    .quad 0x00CF92000000FFFF // 0x18: data
ap_gdt_ptr:
    .word ap_gdt_ptr - ap_gdt - 1
    .long ap_gdt - ap_trampoline_start + 0x8000
ap_trampoline_end:
