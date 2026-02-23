/* SPDX-License-Identifier: MIT */
ENTRY(_start32)

SECTIONS {
    . = 2M; /* Load kernel at 2MB */

    .text : ALIGN(4K) {
        KEEP(*(.multiboot_header))
        *(.text*)
    }

    .rodata : ALIGN(4K) {
        *(.rodata*)
    }

    .data : ALIGN(4K) {
        *(.data*)
    }

    .bss : ALIGN(4K) {
        *(COMMON)
        *(.bss*)
    }
    
    . = ALIGN(4K);
    kernel_end = .;
}
