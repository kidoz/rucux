// SPDX-License-Identifier: MIT
#include <arch/aarch64/gic.hpp>
#include <arch/aarch64/psci.hpp>
#include <arch/aarch64/smp.hpp>
#include <arch/aarch64/timer.hpp>
#include <kernel/cpu/percpu.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <knew.hpp>

extern "C" void aarch64_ap_entry();

// Stacks for secondary cores, indexed by cpu_id. smp.S computes
// base + (cpu_id + 1) * AP_STACK_SIZE, so the array needs one extra slot.
extern "C" {
alignas(16) uint8_t aarch64_ap_stacks[(arch::aarch64::MAX_AP_CPUS + 1) * arch::aarch64::AP_STACK_SIZE];
}

namespace arch::aarch64 {

// smp.S hardcodes this because assembler and C++ cannot share a constant here.
// If one changes, this fails rather than corrupting a secondary's stack.
static_assert(AP_STACK_SIZE == 16384, "AP_STACK_SIZE must match AARCH64_AP_STACK_SIZE in smp.S");

namespace {

kernel::atomic<uint32_t> g_aps_online{0};

// GIC CPU interface base, recorded by the boot CPU. Each core must enable its
// own interface: the registers are banked per-CPU even though the address is
// shared.
uintptr_t g_gic_cpu_base = 0;

} // namespace

extern "C" void aarch64_ap_main(uint64_t cpu_id) {
    // Join the address space the boot CPU built rather than constructing a
    // second one.
    kernel::memory::vmm::enable_on_this_cpu();

    uint64_t mpidr = 0;
    asm volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    kernel::cpu::ap_init(static_cast<uint32_t>(cpu_id), static_cast<uint32_t>(mpidr & 0xFF));

    // Per-CPU banked GIC interface and timer.
    if (g_gic_cpu_base != 0)
        gic_cpu_interface::init(g_gic_cpu_base);
    gic_distributor::enable_irq(TIMER_IRQ);
    generic_timer::init_periodic(1000);

    auto* pcpu = kernel::cpu::this_cpu();

    auto* idle = new kernel::scheduler::thread();
    idle->tid = 0;
    idle->priority = kernel::scheduler::thread_prio::IDLE;
    idle->stack_size = 4096;
    idle->stack_base = reinterpret_cast<uintptr_t>(new uint8_t[idle->stack_size]);
    idle->pml4_phys = 0;

    uint64_t* stack = reinterpret_cast<uint64_t*>((idle->stack_base + idle->stack_size) & ~0xFULL);
    stack -= 12;
    for (int i = 0; i < 12; ++i)
        stack[i] = 0;
    stack[11] = reinterpret_cast<uint64_t>(&kernel::scheduler::idle_task); // x30
    idle->stack_pointer = reinterpret_cast<uintptr_t>(stack);
    idle->state = kernel::scheduler::thread_state::READY;

    pcpu->idle_thread = idle;
    pcpu->current_thread = nullptr;

    g_aps_online.fetch_add(1, kernel::relaxed);
    kernel::print("AP{}: online (MPIDR {})\n", static_cast<uint32_t>(cpu_id),
                  reinterpret_cast<void*>(static_cast<uintptr_t>(mpidr)));

    asm volatile("msr daifclr, #2" ::: "memory");
    for (;;)
        asm volatile("wfi");
}

void smp_boot_aps(uint32_t num_cpus) noexcept {
    g_gic_cpu_base = gic_cpu_interface::base();

    kernel::print("SMP: PSCI version {}, conduit {}\n", psci::version(), psci::uses_hvc() ? "hvc" : "smc");

    if (num_cpus > MAX_AP_CPUS + 1)
        num_cpus = MAX_AP_CPUS + 1;

    for (uint32_t id = 1; id < num_cpus; ++id) {
        // Affinity 0 is the core index on both QEMU `virt` and the S905's
        // single-cluster Cortex-A53 layout.
        const uint64_t target_mpidr = id;
        int32_t rc = psci::cpu_on(target_mpidr, reinterpret_cast<uintptr_t>(&aarch64_ap_entry), id);
        if (rc != psci_ret::SUCCESS)
            kernel::print("SMP: CPU_ON for core {} failed ({})\n", id, rc);
    }

    // Bounded wait; a core that never checks in must not hang boot.
    for (uint32_t spin = 0; spin < 100000000U; ++spin) {
        if (g_aps_online.load(kernel::relaxed) == num_cpus - 1)
            break;
        asm volatile("yield");
    }

    kernel::print("SMP: {} of {} secondary cores online\n", g_aps_online.load(kernel::relaxed), num_cpus - 1);
}

} // namespace arch::aarch64
