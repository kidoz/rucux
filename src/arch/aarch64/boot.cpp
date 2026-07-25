// SPDX-License-Identifier: MIT
#include <arch/aarch64/console.hpp>
#include <arch/aarch64/exception.hpp>
#include <arch/aarch64/gic.hpp>
#include <arch/aarch64/timer.hpp>
#include <arch/aarch64/uart.hpp>
#include <kernel/fdt.hpp>
#include <kernel/print.hpp>
#include <stdint.h>

// Hardware defaults come from the board manifest via meson (see board.yaml).
// They are the authority whenever firmware passes no device tree.
#ifndef RUCUX_UART_BASE
#define RUCUX_UART_BASE 0x09000000 // QEMU `virt` PL011
#endif
#ifndef RUCUX_UART_PL011
#define RUCUX_UART_PL011 1
#endif
#ifndef RUCUX_GIC_DIST_BASE
#define RUCUX_GIC_DIST_BASE 0x08000000
#endif
#ifndef RUCUX_GIC_CPU_BASE
#define RUCUX_GIC_CPU_BASE 0x08010000
#endif

extern "C" {

void kernel_main(uint64_t fdt_addr) {
    using namespace arch::aarch64;

    // Vectors first. Until VBAR_EL1 is set, any fault vectors to address 0 and
    // spins on undefined instructions, turning a diagnosable abort into a
    // silent hang.
    exceptions_init();

    bool has_fdt = fdt_addr != 0 && kernel::fdt::init(reinterpret_cast<void*>(fdt_addr));

    // Console before anything else that can fail, so failures are visible.
    uintptr_t uart_base = RUCUX_UART_BASE;
    bool is_pl011 = RUCUX_UART_PL011 != 0;
    if (has_fdt) {
        uintptr_t fdt_uart = 0;
        bool fdt_pl011 = false;
        if (kernel::fdt::get_uart(&fdt_uart, &fdt_pl011)) {
            uart_base = fdt_uart;
            is_pl011 = fdt_pl011;
        }
    }
    uart::init_dynamic(uart_base, is_pl011);
    console::init_early();

    kernel::print("rucux (aarch64) Initialized!\n");
    kernel::print("FDT: {}\n", reinterpret_cast<void*>(static_cast<uintptr_t>(fdt_addr)));

    uint64_t current_el = 0;
    asm volatile("mrs %0, CurrentEL" : "=r"(current_el));
    kernel::print("Running at EL{}\n", static_cast<uint32_t>((current_el >> 2) & 3));

    uintptr_t gic_dist = RUCUX_GIC_DIST_BASE;
    uintptr_t gic_cpu = RUCUX_GIC_CPU_BASE;

    if (has_fdt) {
        uint64_t mem_base = 0;
        uint64_t mem_size = 0;
        if (kernel::fdt::get_memory(&mem_base, &mem_size)) {
            kernel::print("FDT Memory: base={}, size={} MB\n",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(mem_base)),
                          static_cast<uint32_t>(mem_size / (1024 * 1024)));
            // PMM is not wired up yet: it pulls in the buddy allocator, which
            // needs per-CPU state and the scheduler. Those land with the MMU
            // work rather than in the boot core.
        } else {
            kernel::print("WARNING: no memory node in FDT\n");
        }

        if (!kernel::fdt::get_gic(&gic_dist, &gic_cpu))
            kernel::print("WARNING: no GIC node in FDT; using defaults\n");
    } else {
        kernel::print("No FDT; using board manifest defaults\n");
    }

    kernel::print("GIC: dist={}, cpu={}\n", reinterpret_cast<void*>(gic_dist), reinterpret_cast<void*>(gic_cpu));
    gic_init(gic_dist, gic_cpu);

    gic_distributor::set_priority(TIMER_IRQ, 0);
    gic_distributor::enable_irq(TIMER_IRQ);

    generic_timer::init_periodic(1000);

    // Prove the interrupt path end to end: the tick must actually reach the
    // handler, not merely be programmed.
    while (irq_count() < 5)
        asm volatile("wfi");

    kernel::print("rucux (aarch64) boot complete, 1 CPUs online\n");

    for (;;)
        asm volatile("wfi");
}

} // extern "C"
