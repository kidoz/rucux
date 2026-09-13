/* SPDX-License-Identifier: MIT */
ENTRY(_start)

/* Default matches QEMU `virt`: DRAM base 0x40000000 plus the conventional
   arm64 0x80000 text offset. Boards override via --defsym=KERNEL_LOAD_ADDRESS,
   which meson supplies from board.yaml. */
PHDRS {
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS {
    . = DEFINED(KERNEL_LOAD_ADDRESS) ? KERNEL_LOAD_ADDRESS : 0x40080000;

    .text : ALIGN(4K) {
        KEEP(*(.text.boot))
        *(.text*)
    } :text

    /* The vector table must be 2 KiB aligned for VBAR_EL1. */
    .vectors : ALIGN(2048) {
        KEEP(*(.vectors))
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
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        . = ALIGN(8);
        __bss_end = .;
    } :data

    . = ALIGN(4K);
    kernel_end = .;
}
