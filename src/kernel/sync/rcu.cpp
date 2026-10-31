// SPDX-License-Identifier: MIT
#include <kernel/cpu/percpu.hpp>
#include <kernel/memory/heap.hpp>
#include <kernel/sync/rcu.hpp>

namespace kernel::rcu {

// Grace period counter — incremented when a new grace period starts
static atomic<uint64_t> g_gp_counter{0};

// Per-CPU quiescent state tracking
static atomic<uint64_t> g_cpu_qs[cpu::MAX_CPUS]{};

// Accessed only by the owning CPU, with local IRQs disabled.
struct reader_state {
    uint32_t nesting;
    uintptr_t irq_flags;
};
static reader_state g_readers[cpu::MAX_CPUS]{};

// Deferred callback queue
struct rcu_head {
    rcu_callback cb;
    void* arg;
    uint64_t gp_num; // Grace period after which this can be called
    rcu_head* next;
};

static rcu_head* g_callback_list = nullptr;
static irq_spinlock g_callback_lock;
static atomic<bool> g_callbacks_pending{false};

void init() noexcept {
    g_gp_counter.store(0, relaxed);
    for (uint32_t i = 0; i < cpu::MAX_CPUS; ++i) {
        g_cpu_qs[i].store(0, relaxed);
        g_readers[i] = {};
    }
    g_callback_list = nullptr;
    g_callbacks_pending.store(false, relaxed);
}

void read_lock() noexcept {
    uintptr_t flags = irq_save();
    auto& reader = g_readers[cpu::this_cpu()->cpu_id];
    if (reader.nesting++ == 0) reader.irq_flags = flags;
}

void read_unlock() noexcept {
    auto& reader = g_readers[cpu::this_cpu()->cpu_id];
    if (!reader.nesting) __builtin_trap();
    if (--reader.nesting == 0) {
        uintptr_t flags = reader.irq_flags;
        note_quiescent_state();
        irq_restore(flags);
    }
}

void note_quiescent_state() noexcept {
    uintptr_t flags = irq_save();
    auto* pcpu = cpu::this_cpu();
    if (!g_readers[pcpu->cpu_id].nesting) {
        g_cpu_qs[pcpu->cpu_id].store(g_gp_counter.load(acquire), release);
    }
    irq_restore(flags);
}

// Check if all online CPUs have passed the given grace period.
static bool all_cpus_passed(uint64_t gp) noexcept {
    uint32_t ncpus = cpu::g_cpu_count.load(relaxed);
    for (uint32_t i = 0; i < ncpus; ++i) {
        if (g_cpu_qs[i].load(acquire) < gp) return false;
    }
    return true;
}

void synchronize() noexcept {
    // Start a new grace period
    uint64_t gp = g_gp_counter.fetch_add(1, acq_rel) + 1;

    // Wait until all CPUs have observed this grace period
    while (!all_cpus_passed(gp)) {
        // The updater itself need not wait for a context switch (including UP).
        note_quiescent_state();
        cpu_relax();
    }
}

bool call_rcu(rcu_callback cb, void* arg) noexcept {
    if (!cb) return false;
    auto* head = static_cast<rcu_head*>(kmalloc(sizeof(rcu_head)));
    if (!head) return false;
    head->cb = cb;
    head->arg = arg;
    // One RMW reserves the callback's own epoch even with concurrent updaters.
    head->gp_num = g_gp_counter.fetch_add(1, acq_rel) + 1;

    uintptr_t flags = g_callback_lock.lock();
    head->next = g_callback_list;
    g_callback_list = head;
    g_callbacks_pending.store(true, release);
    g_callback_lock.unlock(flags);
    return true;
}

void process_callbacks() noexcept {
    // Keep empty scheduler passes cheap without racing on the list pointer.
    if (!g_callbacks_pending.load(acquire)) return;
    uintptr_t flags = g_callback_lock.lock();

    rcu_head* ready = nullptr;
    rcu_head** pp = &g_callback_list;
    while (*pp) {
        if (all_cpus_passed((*pp)->gp_num)) {
            rcu_head* h = *pp;
            *pp = h->next;
            h->next = ready;
            ready = h;
        } else {
            pp = &(*pp)->next;
        }
    }

    g_callbacks_pending.store(g_callback_list != nullptr, release);
    g_callback_lock.unlock(flags);

    // Execute callbacks outside the lock
    while (ready) {
        rcu_head* h = ready;
        ready = h->next;
        h->cb(h->arg);
        kfree(h);
    }
}

} // namespace kernel::rcu
