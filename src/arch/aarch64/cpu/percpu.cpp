// SPDX-License-Identifier: MIT
#include <kernel/cpu/percpu.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>

namespace kernel::cpu {

// Global per-CPU array (shared definition for AArch64 builds)
per_cpu g_percpu[MAX_CPUS] = {};
atomic<uint32_t> g_cpu_count{0};

void arch_set_percpu_base(per_cpu* p) noexcept {
    // TPIDR_EL1 — Software Thread ID Register, EL1. Only writable from EL1 and
    // above, so userspace cannot forge the per-CPU pointer. TPIDR_EL0 is left
    // free for userspace TLS.
    asm volatile("msr tpidr_el1, %0" ::"r"(p) : "memory");
    asm volatile("isb" ::: "memory");
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

namespace {

void init_slot(per_cpu* p, uint32_t cpu_id, uint32_t mpidr_aff) noexcept {
    p->self = p;
    p->cpu_id = cpu_id;
    p->apic_id = mpidr_aff; // MPIDR affinity on ARM
    p->current_thread = nullptr;
    p->idle_thread = nullptr;
    p->kernel_stack = 0;
    p->temp_user_rsp = 0;
    p->online = true;
    p->ticks = 0;
    p->total_runnable = 0;
    for (int i = 0; i < scheduler::NUM_PRIOS; ++i)
        p->queues[i].init();

    arch_set_percpu_base(p);
}

} // namespace

void bsp_init() noexcept {
    auto* bsp = &g_percpu[0];
    init_slot(bsp, 0, 0);
    g_cpu_count.store(1, relaxed);

    kernel::print("Per-CPU: BSP (cpu_id=0) initialized, TPIDR_EL1 set\n");
}

void ap_init(uint32_t cpu_id, uint32_t apic_id) noexcept {
    init_slot(&g_percpu[cpu_id], cpu_id, apic_id);
    g_cpu_count.fetch_add(1, relaxed);
}

} // namespace kernel::cpu
