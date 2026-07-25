// SPDX-License-Identifier: MIT
#include <arch/amd64/apic.hpp>
#include <arch/amd64/smp.hpp>
#include <kernel/cpu/percpu.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/sync/spinlock.hpp>
#include <knew.hpp>
#include <lib/string.hpp>

namespace arch::amd64 {

// Trampoline symbols (defined in smp_trampoline.S)
extern "C" {
extern uint8_t ap_trampoline_start[];
extern uint8_t ap_trampoline_end[];
extern uint32_t ap_pml4[];
extern uint64_t ap_stack[];
extern uint64_t ap_entry[];
}

// Fixed physical address where we copy the trampoline (<1MB, page-aligned)
static constexpr uintptr_t TRAMPOLINE_PHYS = 0x8000;

// AP synchronization
static kernel::atomic<uint32_t> g_ap_started{0};

// C++ entry point for each AP — called from the trampoline
extern "C" void ap_main() {
    // Determine our APIC ID
    uint8_t my_apic_id = lapic::id();

    // Find our logical CPU ID (assigned in order from MADT)
    uint32_t my_cpu_id = g_ap_started.fetch_add(1, kernel::relaxed) + 1;

    // Initialize per-CPU data
    kernel::cpu::ap_init(my_cpu_id, my_apic_id);

    // Enable LAPIC on this AP
    lapic::init(kernel::cpu::this_cpu()->self ? static_cast<uintptr_t>(0) // base already set by BSP copy
                                              : 0);
    // Re-init LAPIC with the same base as BSP
    // (The LAPIC base address is the same for all CPUs — it's memory-mapped)
    auto* pcpu = kernel::cpu::this_cpu();

    kernel::print("AP{}: online (APIC ID {})\n", my_cpu_id, my_apic_id);

    // Create idle thread for this AP
    auto* idle = new kernel::scheduler::thread();
    idle->tid = 0;
    idle->stack_size = 4096;
    idle->stack_base = reinterpret_cast<uintptr_t>(new uint8_t[idle->stack_size]);
    idle->pml4_phys = 0;
    idle->state = kernel::scheduler::thread_state::READY;
    pcpu->idle_thread = idle;
    pcpu->current_thread = nullptr;

    // Enable interrupts and enter idle loop
    // (In a full implementation, APs would enter the scheduler here)
    asm volatile("sti");
    while (true) {
        asm volatile("hlt");
    }
}

// Offsets into the trampoline data section (must match smp_trampoline.S layout)
// These are relative to ap_trampoline_start
static uintptr_t trampoline_size() {
    return reinterpret_cast<uintptr_t>(ap_trampoline_end) - reinterpret_cast<uintptr_t>(ap_trampoline_start);
}

void smp_boot_aps(const acpi::madt_info& info, uintptr_t pml4_phys) noexcept {
    if (info.cpu_count <= 1) {
        kernel::print("SMP: Only 1 CPU found, skipping AP boot\n");
        return;
    }

    // Copy trampoline to low memory
    size_t tramp_size = trampoline_size();
    lib::memcpy(reinterpret_cast<void*>(TRAMPOLINE_PHYS), ap_trampoline_start, tramp_size);

    // Patch trampoline data fields.
    // Compute offsets of data fields relative to trampoline start.

    uintptr_t pml4_off = reinterpret_cast<uintptr_t>(ap_pml4) - reinterpret_cast<uintptr_t>(ap_trampoline_start);
    uintptr_t stack_off = reinterpret_cast<uintptr_t>(ap_stack) - reinterpret_cast<uintptr_t>(ap_trampoline_start);
    uintptr_t entry_off = reinterpret_cast<uintptr_t>(ap_entry) - reinterpret_cast<uintptr_t>(ap_trampoline_start);

    // Write PML4 physical address for the AP to use
    *reinterpret_cast<uint32_t*>(TRAMPOLINE_PHYS + pml4_off) = static_cast<uint32_t>(pml4_phys);
    // Write AP kernel stack (allocated per-AP below)
    // Write C++ entry point
    *reinterpret_cast<uint64_t*>(TRAMPOLINE_PHYS + entry_off) = reinterpret_cast<uint64_t>(ap_main);

    uint8_t bsp_apic_id = lapic::id();

    kernel::print("SMP: Booting {} APs (BSP APIC ID={})\n", info.cpu_count - 1, bsp_apic_id);

    for (uint32_t i = 0; i < info.cpu_count; ++i) {
        if (!info.cpus[i].enabled) continue;
        if (info.cpus[i].apic_id == bsp_apic_id) continue;

        uint8_t target = info.cpus[i].apic_id;
        uint32_t before = g_ap_started.load(kernel::relaxed);

        // Allocate a kernel stack for this AP (8KB)
        auto* ap_kstack = new uint8_t[8192];
        *reinterpret_cast<uint64_t*>(TRAMPOLINE_PHYS + stack_off) = reinterpret_cast<uint64_t>(ap_kstack + 8192);

        // Send INIT
        lapic::send_init(target);

        // Wait ~10ms (busy loop)
        for (int j = 0; j < 1000000; j++)
            kernel::cpu_relax();

        // Send SIPI (vector = trampoline page number = 0x8000 / 4096 = 0x08)
        lapic::send_sipi(target, TRAMPOLINE_PHYS / 4096);

        // Wait for AP to start (~100ms timeout)
        for (int j = 0; j < 10000000; j++) {
            if (g_ap_started.load(kernel::relaxed) > before) break;
            kernel::cpu_relax();
        }

        if (g_ap_started.load(kernel::relaxed) <= before) {
            // Retry with second SIPI (per Intel spec)
            lapic::send_sipi(target, TRAMPOLINE_PHYS / 4096);
            for (int j = 0; j < 10000000; j++) {
                if (g_ap_started.load(kernel::relaxed) > before) break;
                kernel::cpu_relax();
            }
        }

        if (g_ap_started.load(kernel::relaxed) > before) {
            kernel::print("SMP: AP (APIC ID {}) started\n", target);
        } else {
            kernel::print("SMP: AP (APIC ID {}) failed to start!\n", target);
        }
    }

    kernel::print("SMP: {} APs online\n", g_ap_started.load(kernel::relaxed));
}

} // namespace arch::amd64
