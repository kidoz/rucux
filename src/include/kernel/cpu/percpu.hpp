// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/sync/spinlock.hpp>
#include <stdint.h>

namespace kernel::scheduler {
struct thread;
enum class thread_prio : uint8_t;
constexpr int NUM_PRIOS = 3;
} // namespace kernel::scheduler

namespace kernel::cpu {

// Maximum number of CPUs supported
inline constexpr uint32_t MAX_CPUS = 64;

// Per-CPU data structure. One instance per logical CPU.
// On amd64: accessed via GS segment base.
// On ARMv7: accessed via TPIDRPRW (CP15 c13,0,c0,4).
// Per-CPU run queue: one FIFO queue per priority level.
// Methods defined out-of-line (scheduler.cpp) because they access thread::next.
struct run_queue {
    scheduler::thread* head;
    scheduler::thread* tail;
    uint32_t count;

    void init() noexcept;
    void enqueue(scheduler::thread* t) noexcept;
    scheduler::thread* dequeue() noexcept;
    bool empty() const noexcept { return head == nullptr; }
};

struct per_cpu {
    per_cpu* self;    // GS:0 self-pointer for fast access
    uint32_t cpu_id;  // Logical CPU ID (0 = BSP)
    uint32_t apic_id; // Hardware APIC/MPIDR ID

    scheduler::thread* current_thread; // Currently running thread on this CPU
    scheduler::thread* idle_thread;    // This CPU's idle thread
    uintptr_t kernel_stack;            // Current kernel stack top — read by syscall_entry at %gs:32 (amd64)
    uintptr_t temp_user_rsp;           // Scratch slot for syscall_entry user rsp save — accessed at %gs:40 (amd64)

    bool online;    // CPU is initialized and running
    uint64_t ticks; // Per-CPU tick count

    // Per-CPU run queues — one per priority level
    run_queue queues[scheduler::NUM_PRIOS];
    uint32_t total_runnable; // Sum of all queue counts

    // Protects this CPU's run queues
    irq_spinlock sched_lock;
};

// Global per-CPU array (indexed by cpu_id)
extern per_cpu g_percpu[MAX_CPUS];

// Number of online CPUs
extern atomic<uint32_t> g_cpu_count;

// Get the per_cpu struct for the calling CPU (fast path via segment register)
inline per_cpu* this_cpu() noexcept {
#if defined(__x86_64__)
    per_cpu* p;
    asm volatile("mov %%gs:0, %0" : "=r"(p)::"memory");
    return p;
#elif defined(__arm__)
    per_cpu* p;
    asm volatile("mrc p15, 0, %0, c13, c0, 4" : "=r"(p)::"memory");
    return p;
#else
    return &g_percpu[0];
#endif
}

// Get per_cpu by CPU id (for cross-CPU access, e.g. IPI target)
inline per_cpu* get_cpu(uint32_t id) noexcept {
    return &g_percpu[id];
}

// Initialize BSP (Bootstrap Processor) per-CPU data.
// Must be called early in boot before the scheduler.
void bsp_init() noexcept;

// Called by each AP (Application Processor) during SMP bring-up.
void ap_init(uint32_t cpu_id, uint32_t apic_id) noexcept;

// Architecture-specific: set the per-CPU base register (GS / TPIDRPRW)
void arch_set_percpu_base(per_cpu* p) noexcept;

} // namespace kernel::cpu
