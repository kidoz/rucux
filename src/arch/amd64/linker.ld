/* SPDX-License-Identifier: MIT */
ENTRY(_start32)

PHDRS {
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS {
    . = 2M; /* Load kernel at 2MB */

    .text : ALIGN(4K) {
        KEEP(*(.multiboot_header))
        *(.text*)
    } :text

    .rodata : ALIGN(4K) {
        *(.rodata*)
        *(.eh_frame*)
    } :rodata

    .data : ALIGN(4K) {
        *(.data*)
        *(.got*)
    } :data

    .bss : ALIGN(4K) {
        *(COMMON)
        *(.bss*)
    } :data
    
    . = ALIGN(4K);
    kernel_end = .;
}
