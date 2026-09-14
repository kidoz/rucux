// SPDX-License-Identifier: MIT
#include <arch/aarch64/exception.hpp>
#include <arch/aarch64/gic.hpp>
#include <arch/aarch64/timer.hpp>
#include <kernel/cpu/percpu.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/time.hpp>

extern "C" void* aarch64_vector_table;

namespace arch::aarch64 {

namespace {

kernel::atomic<uint64_t> g_irq_count{0};

// Preemption is gated because exceptions_init() unmasks IRQs early, so the
// timer is already ticking while kernel_main is still bringing the system up.
// schedule() with no current thread switches to idle via
// switch_context(nullptr, ...), which abandons the boot context permanently —
// so it must stay off until boot is ready to hand over.
kernel::atomic<bool> g_preemption_enabled{false};

// Interrupt IDs 1020-1023 are reserved; 1023 means "spurious".
constexpr uint32_t GIC_SPURIOUS_MIN = 1020;

const char* fault_name(uint64_t index) noexcept {
    switch (index) {
    case 0:
    case 4:
    case 8:
    case 12:
        return "synchronous";
    case 1:
    case 5:
    case 9:
    case 13:
        return "IRQ";
    case 2:
    case 6:
    case 10:
    case 14:
        return "FIQ";
    default:
        return "SError";
    }
}

} // namespace

void exceptions_init() noexcept {
    auto vbar = reinterpret_cast<uintptr_t>(&aarch64_vector_table);
    asm volatile("msr vbar_el1, %0" ::"r"(vbar) : "memory");
    asm volatile("isb" ::: "memory");
    // Unmask IRQ; keep FIQ/SError masked until there is something to handle.
    asm volatile("msr daifclr, #2" ::: "memory");
}

uint64_t irq_count() noexcept {
    return g_irq_count.load(kernel::relaxed);
}

void enable_preemption() noexcept {
    g_preemption_enabled.store(true, kernel::release);
}

} // namespace arch::aarch64

extern "C" void aarch64_irq_handler() {
    uint32_t irq = arch::aarch64::gic_cpu_interface::acknowledge();
    uint32_t id = irq & 0x3FF;

    if (id >= arch::aarch64::GIC_SPURIOUS_MIN) return; // spurious; no EOI required

    arch::aarch64::g_irq_count.fetch_add(1, kernel::relaxed);

    if (id == arch::aarch64::TIMER_IRQ) {
        arch::aarch64::generic_timer::rearm();
        auto* pcpu = kernel::cpu::this_cpu();
        pcpu->ticks++;
        if (pcpu->cpu_id == 0) kernel::time_manager::tick();
        if (pcpu->cpu_id != 0 && pcpu->ticks == 5) kernel::print("AP{}: timer IRQ verified\n", pcpu->cpu_id);
        // EOI before switching away: schedule() may not return to this frame,
        // and leaving the interrupt active would block every later one.
        arch::aarch64::gic_cpu_interface::end_of_interrupt(irq);
        if (arch::aarch64::g_preemption_enabled.load(kernel::acquire)) kernel::scheduler::scheduler::schedule();
        return;
    }

    kernel::scheduler::scheduler::wake_irq_waiters(static_cast<uint8_t>(id));
    arch::aarch64::gic_cpu_interface::end_of_interrupt(irq);
}

// Reached only if a kernel thread's entry function returns, which none should.
extern "C" void aarch64_thread_exit() {
    kernel::scheduler::scheduler::exit(0);
}

extern "C" void aarch64_fault_handler(uint64_t index, uint64_t esr, uint64_t elr) {
    kernel::print("\n*** AArch64 {} fault (vector {}) ***\n", arch::aarch64::fault_name(index), index);
    kernel::print("ESR_EL1 = {}\n", reinterpret_cast<void*>(static_cast<uintptr_t>(esr)));
    kernel::print("ELR_EL1 = {}\n", reinterpret_cast<void*>(static_cast<uintptr_t>(elr)));
    kernel::print("EC      = {}\n", static_cast<uint32_t>((esr >> 26) & 0x3F));
    kernel::print("System halted.\n");
}
