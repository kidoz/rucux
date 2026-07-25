// SPDX-License-Identifier: MIT
//
// Secondary-core entry point, jumped to by firmware in response to PSCI CPU_ON.
//
// The core arrives with translation off and caches cold, so this runs
// identity-mapped — which is why the kernel's low-4 GiB identity map matters
// beyond boot. x0 holds the context_id passed to CPU_ON: the logical CPU id.

// Must match arch::aarch64::AP_STACK_SIZE; smp.cpp static_asserts the value.
.set AARCH64_AP_STACK_SIZE, 16384

.section .text.boot, "ax"
.global aarch64_ap_entry
.type aarch64_ap_entry, %function

aarch64_ap_entry:
    mov     x19, x0                     // preserve cpu_id across the EL drop

    ldr     x0, =0x30d00800             // SCTLR_EL1: MMU/caches off, RES1 set
    msr     sctlr_el1, x0

    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #2
    b.ne    ap_at_el1

    // Firmware may start secondaries at EL2 even when the boot CPU has already
    // dropped, so handle it the same way start.S does.
    mrs     x0, hcr_el2
    orr     x0, x0, #(1 << 31)          // RW: EL1 is AArch64
    msr     hcr_el2, x0

    mrs     x0, cnthctl_el2
    orr     x0, x0, #3                  // EL1PCTEN | EL1PCEN
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr

    mov     x0, #0x3c5                  // DAIF masked, return to EL1h
    msr     spsr_el2, x0
    adr     x0, ap_at_el1
    msr     elr_el2, x0
    eret

ap_at_el1:
    // Each core takes its own slot from the AP stack array: sp = base +
    // (cpu_id + 1) * AARCH64_AP_STACK_SIZE, so slot N grows down into slot N-1's
    // upper bound.
    ldr     x0, =aarch64_ap_stacks
    mov     x1, #AARCH64_AP_STACK_SIZE
    add     x2, x19, #1
    madd    x0, x1, x2, x0
    mov     sp, x0

    mov     x0, x19
    bl      aarch64_ap_main

ap_halt:
    wfe
    b       ap_halt
