/* SPDX-License-Identifier: MIT */
ENTRY(_start)

SECTIONS {
    . = DEFINED(KERNEL_LOAD_ADDRESS) ? KERNEL_LOAD_ADDRESS : 0x8000;

    .text : ALIGN(4K) {
        KEEP(*(.text.boot))
        KEEP(*(.vectors))
        *(.text*)
    }

    .rodata : ALIGN(4K) {
        *(.rodata*)
    }

    .data : ALIGN(4K) {
        *(.data*)
    }

    .bss : ALIGN(4K) {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }

    . = ALIGN(4K);
    kernel_end = .;
}
