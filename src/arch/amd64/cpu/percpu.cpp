// SPDX-License-Identifier: MIT
#include <kernel/cpu/percpu.hpp>
#include <kernel/print.hpp>

namespace kernel::cpu {

// MSR addresses for GS base
static constexpr uint32_t MSR_GS_BASE = 0xC0000101;
static constexpr uint32_t MSR_KERNEL_GS_BASE = 0xC0000102;

static inline void wrmsr(uint32_t msr, uint64_t value) noexcept {
    uint32_t lo = value & 0xFFFFFFFF;
    uint32_t hi = (value >> 32) & 0xFFFFFFFF;
    asm volatile("wrmsr" :: "c"(msr), "a"(lo), "d"(hi) : "memory");
}

void arch_set_percpu_base(per_cpu* p) noexcept {
    // Set GS base to point to per_cpu struct
    // KERNEL_GS_BASE is swapped in/out by swapgs on syscall entry/exit
    wrmsr(MSR_GS_BASE, reinterpret_cast<uint64_t>(p));
    // Also set KERNEL_GS_BASE so swapgs works correctly:
    // In kernel context, GS base = per_cpu.
    // On syscall entry (swapgs), we swap GS ↔ KERNEL_GS_BASE.
    // So KERNEL_GS_BASE should hold the per-CPU pointer when we're in user mode.
    wrmsr(MSR_KERNEL_GS_BASE, reinterpret_cast<uint64_t>(p));
}

// Global per-CPU array
per_cpu g_percpu[MAX_CPUS] = {};
atomic<uint32_t> g_cpu_count{0};

void bsp_init() noexcept {
    auto* bsp = &g_percpu[0];
    bsp->self = bsp;
    bsp->cpu_id = 0;
    bsp->apic_id = 0;
    bsp->current_thread = nullptr;
    bsp->idle_thread = nullptr;
    bsp->kernel_stack = 0;
    bsp->online = true;
    bsp->ticks = 0;

    arch_set_percpu_base(bsp);
    g_cpu_count.store(1, relaxed);

    kernel::print("Per-CPU: BSP (cpu_id=0) initialized, GS base set\n");
}

void ap_init(uint32_t cpu_id, uint32_t apic_id) noexcept {
    auto* ap = &g_percpu[cpu_id];
    ap->self = ap;
    ap->cpu_id = cpu_id;
    ap->apic_id = apic_id;
    ap->current_thread = nullptr;
    ap->idle_thread = nullptr;
    ap->kernel_stack = 0;
    ap->online = true;
    ap->ticks = 0;

    arch_set_percpu_base(ap);
    g_cpu_count.fetch_add(1, relaxed);
}

} // namespace kernel::cpu
