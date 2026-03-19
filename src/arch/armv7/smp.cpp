// SPDX-License-Identifier: MIT
#include <arch/armv7/gic.hpp>
#include <arch/armv7/psci.hpp>
#include <arch/armv7/smp.hpp>
#include <arch/armv7/timer.hpp>
#include <kernel/cpu/percpu.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/sync/spinlock.hpp>
#include <knew.hpp>

namespace arch::armv7 {

// Synchronization for AP startup
static kernel::atomic<uint32_t> g_ap_started{0};

// C++ entry point for each secondary core (called from AP trampoline)
extern "C" void ap_entry_arm(uint32_t cpu_id) {
    // Initialize per-CPU data
    // Use cpu_id as both logical and hardware ID (simplified — real code reads MPIDR)
    kernel::cpu::ap_init(cpu_id, cpu_id);

    // Initialize GIC CPU interface for this core
    // The distributor base is already initialized by the BSP.
    // Each AP needs its own CPU interface init (banked registers).
    // The CPU interface address is the same for all cores (banked by hardware).

    // Enable per-CPU timer
    generic_timer::init_periodic(1000);

    auto* pcpu = kernel::cpu::this_cpu();

    // Create idle thread for this core
    auto* idle = new kernel::scheduler::thread();
    idle->tid = 0;
    idle->stack_size = 4096;
    idle->stack_base = reinterpret_cast<uintptr_t>(new uint8_t[idle->stack_size]);
    idle->pml4_phys = 0;

    // Set up initial stack frame for ARMv7 context switch
    uint32_t* stack = reinterpret_cast<uint32_t*>(idle->stack_base + idle->stack_size);
    // idle_task address will be the first "pc" popped
    extern void idle_task();
    *(--stack) = reinterpret_cast<uint32_t>(idle_task); // pc
    for (int i = 0; i < 8; ++i) *(--stack) = 0;        // r4-r11
    idle->stack_pointer = reinterpret_cast<uintptr_t>(stack);
    idle->state = kernel::scheduler::thread_state::READY;

    pcpu->idle_thread = idle;
    pcpu->current_thread = nullptr;

    g_ap_started.fetch_add(1, kernel::relaxed);
    kernel::print("AP{}: online\n", cpu_id);

    // Enable interrupts and enter idle loop
    asm volatile("cpsie i");
    while (true) {
        asm volatile("wfi");
    }
}

void smp_boot_aps(uint32_t num_cpus) noexcept {
    if (num_cpus <= 1) {
        kernel::print("SMP: Single CPU, skipping AP boot\n");
        return;
    }

    // Check PSCI availability
    int32_t ver = psci::version();
    if (ver < 0) {
        kernel::print("SMP: PSCI not available (ret={})\n", ver);
        return;
    }
    kernel::print("SMP: PSCI version 0x{x}\n", reinterpret_cast<void*>(static_cast<uintptr_t>(ver)));

    kernel::print("SMP: Booting {} secondary cores\n", num_cpus - 1);

    for (uint32_t i = 1; i < num_cpus; ++i) {
        uint32_t before = g_ap_started.load(kernel::relaxed);

        // Allocate a kernel stack for this AP (8 KB)
        auto* ap_stack = new uint8_t[8192];
        uintptr_t sp = reinterpret_cast<uintptr_t>(ap_stack + 8192);
        (void)sp; // The AP will use its own stack setup

        // PSCI CPU_ON: target=MPIDR affinity (simplified: core index),
        // entry=ap_entry_arm, context_id=cpu_id (passed in r0)
        int32_t ret = psci::cpu_on(i, reinterpret_cast<uintptr_t>(ap_entry_arm), i);

        if (ret == psci_ret::SUCCESS || ret == psci_ret::ALREADY_ON) {
            // Wait for AP to signal it's online (~100ms timeout)
            for (int j = 0; j < 10000000; j++) {
                if (g_ap_started.load(kernel::relaxed) > before) break;
                kernel::cpu_relax();
            }

            if (g_ap_started.load(kernel::relaxed) > before) {
                kernel::print("SMP: CPU {} started\n", i);
            } else {
                kernel::print("SMP: CPU {} start timeout\n", i);
            }
        }
    }

    kernel::print("SMP: {} APs online\n", g_ap_started.load(kernel::relaxed));
}

} // namespace arch::armv7
