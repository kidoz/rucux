// SPDX-License-Identifier: MIT
//
// AArch64 kernel entry.
//
// Firmware (QEMU -kernel, or U-Boot `bootelf` on Odroid C2) enters here with
// the Linux arm64 boot contract: x0 holds the FDT physical address. Entry may
// be at EL2 (QEMU `virt` with virtualization=on, most U-Boot builds) or at EL1.
// Both are handled: EL2 is dropped to EL1h before any C++ runs.

.section .text.boot, "ax"
.global _start
.type _start, %function

_start:
    mov     x19, x0                     // preserve FDT pointer across the EL drop

    // Park every core except the boot CPU (Aff0 == 0). Secondaries are brought
    // up later via PSCI; until then they must not touch the shared stack.
    mrs     x1, mpidr_el1
    and     x1, x1, #0xFF
    cbz     x1, boot_cpu
park:
    wfe
    b       park

boot_cpu:
    // SCTLR_EL1: MMU off, caches off, but RES1 bits set. Establishes a known
    // state regardless of what firmware left behind.
    ldr     x0, =0x30d00800
    msr     sctlr_el1, x0

    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #2
    b.ne    at_el1                      // already EL1 — nothing to drop

    // ── EL2 → EL1 ──────────────────────────────────────────────────────────
    mrs     x0, hcr_el2
    orr     x0, x0, #(1 << 31)          // RW: EL1 executes in AArch64
    msr     hcr_el2, x0

    // Let EL1 read the physical counter and keep the virtual offset at zero,
    // otherwise CNTVCT_EL0 reads back skewed once we drop down.
    mrs     x0, cnthctl_el2
    orr     x0, x0, #3                  // EL1PCTEN | EL1PCEN
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr

    mov     x0, #0x3c5                  // DAIF masked, return to EL1h
    msr     spsr_el2, x0
    adr     x0, at_el1
    msr     elr_el2, x0
    eret

at_el1:
    // SP_EL1. Set after the drop so it applies to the EL we actually run in.
    ldr     x0, =boot_stack_top
    mov     sp, x0

    // Zero BSS. Linker guarantees both symbols are 8-byte aligned.
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
bss_loop:
    cmp     x0, x1
    b.hs    bss_done
    str     xzr, [x0], #8
    b       bss_loop
bss_done:

    mov     x0, x19                     // kernel_main(fdt)
    bl      kernel_main

hang:
    wfe
    b       hang

.section .bss.boot_stack, "aw", %nobits
.balign 16
boot_stack:
    .space 16384
boot_stack_top:
