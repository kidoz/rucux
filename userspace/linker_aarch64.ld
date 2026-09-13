/* SPDX-License-Identifier: MIT */
ENTRY(_start)

/* The amd64 script links userspace at 0x8000000000, which is exactly 2^39 and
   therefore out of range here: the kernel configures TCR_EL1 with T0SZ=25, so
   TTBR0 covers a 39-bit VA (max 0x7FFFFFFFFF). 64 GiB sits well inside that
   window and clear of the 4 GiB identity map the kernel installs. */
PHDRS {
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS {
    . = 0x1000000000;

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
