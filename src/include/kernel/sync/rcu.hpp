// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/cpu/percpu.hpp>
#include <kernel/sync/atomic.hpp>
#include <kernel/sync/spinlock.hpp>
#include <stdint.h>

namespace kernel::rcu {

// Non-preemptible RCU: read sections save/disable IRQs and may nest. Readers
// must not block, yield, migrate, or call synchronize(). Keep sections short.
// Publish protected pointers with atomic<T*>::store(release), and acquire-load
// them inside a read section. Unlink an object before requesting reclamation.
// CPU IDs must be contiguous; CPU hot-unplug is not supported.

// Boot/test initialization only: no readers, callbacks, or updaters may exist.
void init() noexcept;

void read_lock() noexcept;
void read_unlock() noexcept;

// Writer API — synchronous wait for grace period
void synchronize() noexcept;

// Callback-based deferred free. False means allocation failed or cb was null;
// the caller retains ownership and must not reclaim the object prematurely.
// Callbacks run outside the list lock, in the processing caller's IRQ context,
// and must not block. They may enqueue another callback.
using rcu_callback = void (*)(void* arg);
[[nodiscard]] bool call_rcu(rcu_callback cb, void* arg) noexcept;

// Called on scheduler passes/direct switches and the outermost read unlock.
// Does not acknowledge a grace period while this CPU has an active reader.
void note_quiescent_state() noexcept;

// Called periodically (e.g., from timer IRQ) to process callbacks
// whose grace period has elapsed.
void process_callbacks() noexcept;

} // namespace kernel::rcu
