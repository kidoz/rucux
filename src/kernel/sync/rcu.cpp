// SPDX-License-Identifier: MIT
#include <kernel/cpu/percpu.hpp>
#include <kernel/sync/rcu.hpp>

namespace kernel::rcu {

// Grace period counter — incremented when a new grace period starts
static atomic<uint64_t> g_gp_counter{0};

// Per-CPU quiescent state tracking
static atomic<uint64_t> g_cpu_qs[cpu::MAX_CPUS]{};

// Deferred callback queue
struct rcu_head {
    rcu_callback cb;
    void* arg;
    uint64_t gp_num; // Grace period after which this can be called
    rcu_head* next;
};

static rcu_head* g_callback_list = nullptr;
static irq_spinlock g_callback_lock;

void init() noexcept {
    g_gp_counter.store(0, relaxed);
    for (uint32_t i = 0; i < cpu::MAX_CPUS; ++i)
        g_cpu_qs[i].store(0, relaxed);
}

// Called on every context switch — this CPU has passed a quiescent state.
void note_quiescent_state() noexcept {
    auto* pcpu = cpu::this_cpu();
    g_cpu_qs[pcpu->cpu_id].store(g_gp_counter.load(relaxed), release);
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
        cpu_relax();
    }
}

void call_rcu(rcu_callback cb, void* arg) noexcept {
    auto* head = new rcu_head();
    head->cb = cb;
    head->arg = arg;
    head->gp_num = g_gp_counter.load(relaxed) + 1;

    // Start a new grace period so callbacks will eventually be processed
    g_gp_counter.fetch_add(1, relaxed);

    uintptr_t flags = g_callback_lock.lock();
    head->next = g_callback_list;
    g_callback_list = head;
    g_callback_lock.unlock(flags);
}

void process_callbacks() noexcept {
    // Quick check: anything pending?
    if (!g_callback_list) return;

    uint64_t current_gp = g_gp_counter.load(relaxed);

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

    g_callback_lock.unlock(flags);

    // Execute callbacks outside the lock
    while (ready) {
        rcu_head* h = ready;
        ready = h->next;
        h->cb(h->arg);
        delete h;
    }

    (void)current_gp;
}

} // namespace kernel::rcu
