/* SPDX-License-Identifier: MIT */
ENTRY(_start)

PHDRS {
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS {
    . = 0x8000000000;
    
    .text : ALIGN(4K) {
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
}
