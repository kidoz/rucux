// SPDX-License-Identifier: MIT
// AArch64 userspace entry.
//
// The kernel enters here via ERET with SP_EL0 already set. argc/argv are not
// passed yet, so main() is called with zeros — matching the amd64 crt0.

.section .text
.global _start
.type _start, %function

_start:
    mov     x0, #0                  // argc
    mov     x1, #0                  // argv
    bl      main

    // exit(main's return value). SYS_EXIT is 0; the number goes in x8.
    mov     x8, #0
    svc     #0

    // The kernel should never return from exit; spin rather than run off.
1:  b       1b
