// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/sync/spinlock.hpp>
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::scheduler {
struct thread;
} // namespace kernel::scheduler

namespace kernel::sync {

// Scalable futex implementation using a hash table of wait queues.
// Hash on (address_space, virtual_address) for O(1) lookup.

struct futex_waiter {
    scheduler::thread* thread;
    uintptr_t addr;       // Virtual address being waited on
    uintptr_t addr_space; // Address space (pml4_phys) for disambiguation
    futex_waiter* next;   // Next waiter in bucket chain
};

struct futex_bucket {
    irq_spinlock lock;
    futex_waiter* head;
};

// Number of hash buckets — power of 2 for fast modulo
inline constexpr size_t FUTEX_HASH_SIZE = 256;

void futex_init() noexcept;

// Wait on a futex: block if *uaddr == val.
// Returns 0 on success (was woken), -1 on value mismatch.
long futex_wait(uint32_t* uaddr, uint32_t val) noexcept;

// Wake up to `count` threads waiting on uaddr.
// Returns number of threads woken.
long futex_wake(uint32_t* uaddr, uint32_t count) noexcept;

} // namespace kernel::sync
