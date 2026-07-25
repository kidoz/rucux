// SPDX-License-Identifier: MIT
#include <kernel/cpu/percpu.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>

namespace kernel::cpu {

// Global per-CPU array (shared definition for ARMv7 builds)
per_cpu g_percpu[MAX_CPUS] = {};
atomic<uint32_t> g_cpu_count{0};

void arch_set_percpu_base(per_cpu* p) noexcept {
    // TPIDRPRW — Thread ID Register (Privileged, Read/Write)
    // Only accessible from PL1 (kernel). Perfect for per-CPU pointer.
    asm volatile("mcr p15, 0, %0, c13, c0, 4" ::"r"(p) : "memory");
}

void run_queue::init() noexcept {
    head = tail = nullptr;
    count = 0;
}

void run_queue::enqueue(scheduler::thread* t) noexcept {
    t->next = nullptr;
    if (!head) {
        head = tail = t;
    } else {
        tail->next = t;
        tail = t;
    }
    count++;
}

scheduler::thread* run_queue::dequeue() noexcept {
    if (!head) return nullptr;
    auto* t = head;
    head = t->next;
    if (!head) tail = nullptr;
    t->next = nullptr;
    count--;
    return t;
}

void bsp_init() noexcept {
    auto* bsp = &g_percpu[0];
    bsp->self = bsp;
    bsp->cpu_id = 0;
    bsp->apic_id = 0; // MPIDR Aff0 for ARM
    bsp->current_thread = nullptr;
    bsp->idle_thread = nullptr;
    bsp->kernel_stack = 0;
    bsp->temp_user_rsp = 0;
    bsp->online = true;
    bsp->ticks = 0;
    bsp->total_runnable = 0;
    for (int i = 0; i < scheduler::NUM_PRIOS; ++i)
        bsp->queues[i].init();

    arch_set_percpu_base(bsp);
    g_cpu_count.store(1, relaxed);

    kernel::print("Per-CPU: BSP (cpu_id=0) initialized, TPIDRPRW set\n");
}

void ap_init(uint32_t cpu_id, uint32_t apic_id) noexcept {
    auto* ap = &g_percpu[cpu_id];
    ap->self = ap;
    ap->cpu_id = cpu_id;
    ap->apic_id = apic_id;
    ap->current_thread = nullptr;
    ap->idle_thread = nullptr;
    ap->kernel_stack = 0;
    ap->temp_user_rsp = 0;
    ap->online = true;
    ap->ticks = 0;
    ap->total_runnable = 0;
    for (int i = 0; i < scheduler::NUM_PRIOS; ++i)
        ap->queues[i].init();

    arch_set_percpu_base(ap);
    g_cpu_count.fetch_add(1, relaxed);
}

} // namespace kernel::cpu
