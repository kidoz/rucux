// SPDX-License-Identifier: MIT
#include <kernel/memory/user_access.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/sync/futex.hpp>

namespace kernel::sync {

static futex_bucket g_futex_table[FUTEX_HASH_SIZE];

static size_t futex_hash(uintptr_t addr_space, uintptr_t addr) noexcept {
    // FNV-1a-style hash combining address space and virtual address
    uint64_t h = 14695981039346656037ULL;
    h ^= addr_space;
    h *= 1099511628211ULL;
    h ^= addr;
    h *= 1099511628211ULL;
    return static_cast<size_t>(h) & (FUTEX_HASH_SIZE - 1);
}

void futex_init() noexcept {
    for (size_t i = 0; i < FUTEX_HASH_SIZE; ++i)
        g_futex_table[i].head = nullptr;
}

long futex_wait(uint32_t* uaddr, uint32_t val) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    if (!t) return -1;

    uintptr_t addr = reinterpret_cast<uintptr_t>(uaddr);
    uintptr_t as = t->pml4_phys;
    size_t idx = futex_hash(as, addr);
    auto& bucket = g_futex_table[idx];

    // Check value under bucket lock to prevent lost wakeups
    uintptr_t flags = bucket.lock.lock();

    uint32_t observed = 0;
    if (!memory::copy_from_user(&observed, uaddr, sizeof(observed))) {
        bucket.lock.unlock(flags);
        return -14;
    }
    if (observed != val) {
        bucket.lock.unlock(flags);
        return -1; // Value changed — don't sleep
    }

    if (!t->begin_interruptible_wait()) {
        bucket.lock.unlock(flags);
        return -4;
    }

    // Allocate waiter on stack (it's valid while we're blocked)
    futex_waiter waiter;
    waiter.thread = t;
    waiter.addr = addr;
    waiter.addr_space = as;
    waiter.next = bucket.head;
    bucket.head = &waiter;

    bucket.lock.unlock(flags);

    // BLOCKED was published before dropping the bucket lock. A wake arriving
    // before schedule() must not be overwritten by another block().
    scheduler::scheduler::schedule();

    flags = bucket.lock.lock();
    auto** link = &bucket.head;
    while (*link && *link != &waiter)
        link = &(*link)->next;
    if (*link) *link = waiter.next;
    t->end_interruptible_wait();
    bucket.lock.unlock(flags);
    return t->fatal_signal_pending() ? -4 : 0;
}

long futex_wake(uint32_t* uaddr, uint32_t count) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    if (!t) return -1;

    uintptr_t addr = reinterpret_cast<uintptr_t>(uaddr);
    uintptr_t as = t->pml4_phys;
    size_t idx = futex_hash(as, addr);
    auto& bucket = g_futex_table[idx];

    uintptr_t flags = bucket.lock.lock();

    long woken = 0;
    futex_waiter** pp = &bucket.head;
    while (*pp && woken < static_cast<long>(count)) {
        futex_waiter* w = *pp;
        if (w->addr == addr && w->addr_space == as) {
            // Remove from wait list
            *pp = w->next;
            // Unblock the thread
            scheduler::scheduler::unblock(w->thread);
            woken++;
        } else {
            pp = &w->next;
        }
    }

    bucket.lock.unlock(flags);
    return woken;
}

} // namespace kernel::sync
