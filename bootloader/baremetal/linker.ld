ENTRY(_start)

SECTIONS
{
    /* Amlogic S905 typically loads BL33 at 0x01000000 */
    . = 0x01000000;

    .text : ALIGN(4K) {
        KEEP(*(.text.boot))
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
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    /* Provide a 16KB stack */
    . = ALIGN(16);
    . += 0x4000;
    __stack_top = .;
}
