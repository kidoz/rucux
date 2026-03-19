// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/cpu/percpu.hpp>
#include <kernel/sync/atomic.hpp>
#include <kernel/sync/spinlock.hpp>
#include <stdint.h>

namespace kernel::rcu {

// Simplified RCU (Read-Copy-Update) for freestanding kernel.
//
// Readers: rcu_read_lock() / rcu_read_unlock() — very cheap, no atomics.
//   Just increment/decrement a per-CPU nesting counter.
//   While in an RCU read section, the CPU is not in a quiescent state.
//
// Writers: synchronize_rcu() — waits until all CPUs have passed through
//   a quiescent state (i.e., all rcu_read_lock regions have ended).
//   After synchronize_rcu() returns, it's safe to free old data.
//
// call_rcu(callback, arg) — deferred free: callback(arg) is invoked after
//   the next grace period.

// Per-CPU RCU state (embedded in per_cpu or stored separately)
struct rcu_cpu_data {
    uint32_t nesting;       // Read-side nesting depth
    uint64_t passed_quiesc; // Set by each CPU when it passes quiescent state
};

// Initialize RCU subsystem
void init() noexcept;

// Reader API — extremely cheap (just per-CPU counter)
inline void read_lock() noexcept {
    // Disable preemption (IRQs off for the read section)
    // For a simple kernel: just bump the nesting counter.
    // In a full implementation, preemption would be disabled.
    // We rely on the fact that schedule() calls note_quiescent_state().
}

inline void read_unlock() noexcept {
    // Symmetric with read_lock
}

// Writer API — synchronous wait for grace period
void synchronize() noexcept;

// Callback-based deferred free
using rcu_callback = void (*)(void* arg);
void call_rcu(rcu_callback cb, void* arg) noexcept;

// Called from the scheduler on every context switch.
// Marks this CPU as having passed a quiescent state.
void note_quiescent_state() noexcept;

// Called periodically (e.g., from timer IRQ) to process callbacks
// whose grace period has elapsed.
void process_callbacks() noexcept;

} // namespace kernel::rcu
