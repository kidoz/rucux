// SPDX-License-Identifier: MIT
#include <arch/armv7/gic.hpp>
#include <arch/armv7/timer.hpp>
#include <kernel/cpu/percpu.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <stdint.h>

namespace arch::armv7 {

extern "C" void vector_table();

static constexpr uint32_t TIMER_VIRQ = 27;

// ─── Exception handlers (called from assembly stubs) ───────────────────────

// Defined in syscall.cpp
extern "C" long syscall_dispatch(long, long, long, long, long, long, long);

extern "C" void svc_handler(uint32_t num, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                            uint32_t a6) noexcept {
    syscall_dispatch(num, a1, a2, a3, a4, a5, a6);
}

extern "C" void data_abort_handler(uint32_t fault_addr, uint32_t dfsr) noexcept {
    // DFSR bits [3:0] + bit [10] encode the fault type
    uint32_t status = (dfsr & 0xF) | ((dfsr >> 6) & 0x10);

    // Translation fault (page not present) — try demand paging
    // LPAE status codes: 0b00101 (L1), 0b00110 (L2), 0b00111 (L3)
    bool is_translation = (status == 0x05 || status == 0x06 || status == 0x07);

    if (is_translation) {
        bool write = (dfsr >> 11) & 1;        // WnR bit
        uint64_t error = write ? 0x02 : 0x00; // Mimic x86 error code format
        if (kernel::memory::vmm::handle_page_fault(fault_addr, error)) return;
    }

    kernel::print(
        "Data Abort: addr=0x{x} DFSR=0x{x} status=0x{x}\n", reinterpret_cast<void*>(static_cast<uintptr_t>(fault_addr)),
        reinterpret_cast<void*>(static_cast<uintptr_t>(dfsr)), reinterpret_cast<void*>(static_cast<uintptr_t>(status)));
    while (true)
        asm volatile("wfi");
}

extern "C" void prefetch_abort_handler(uint32_t fault_addr, uint32_t ifsr) noexcept {
    uint32_t status = (ifsr & 0xF) | ((ifsr >> 6) & 0x10);
    bool is_translation = (status == 0x05 || status == 0x06 || status == 0x07);

    if (is_translation) {
        if (kernel::memory::vmm::handle_page_fault(fault_addr, 0)) return;
    }

    kernel::print("Prefetch Abort: addr=0x{x} IFSR=0x{x}\n",
                  reinterpret_cast<void*>(static_cast<uintptr_t>(fault_addr)),
                  reinterpret_cast<void*>(static_cast<uintptr_t>(ifsr)));
    while (true)
        asm volatile("wfi");
}

extern "C" void undefined_handler() noexcept {
    kernel::print("Undefined Instruction exception\n");
    while (true)
        asm volatile("wfi");
}

extern "C" void irq_handler_arm() noexcept {
    uint32_t irq_id = gic_cpu_interface::acknowledge();
    uint32_t irq_num = irq_id & 0x3FF;

    if (irq_num == 1023) return; // Spurious

    if (irq_num == TIMER_VIRQ) {
        kernel::cpu::this_cpu()->ticks++;
        generic_timer::set_timer(generic_timer::get_frequency() / 1000);
        kernel::scheduler::scheduler::schedule();
    } else {
        kernel::scheduler::scheduler::wake_irq_waiters(static_cast<uint8_t>(irq_num));
    }

    gic_cpu_interface::end_of_interrupt(irq_id);
}

extern "C" void fiq_handler() noexcept {
    kernel::print("FIQ (not supported)\n");
    while (true)
        asm volatile("wfi");
}

void exceptions_init() noexcept {
    // VBAR is set by the assembly vector table linkage
    asm volatile("mcr p15, 0, %0, c12, c0, 0" ::"r"(&vector_table));

    // Ensure exception vectors use high vectors (0xFFFF0000) disabled
    uint32_t sctlr;
    asm volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    sctlr &= ~(1 << 13); // V=0: use VBAR
    asm volatile("mcr p15, 0, %0, c1, c0, 0" ::"r"(sctlr));

    asm volatile("dsb sy; isb");
}

} // namespace arch::armv7
