// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace kernel::cpu {
struct per_cpu;
}
namespace kernel::scheduler {
struct thread;

// All queue links and execution claims share one IRQ-safe dispatch lock.
// Lock order: thread-list / subsystem / wait locks -> dispatch. Dispatch never
// acquires those locks, calls exit callbacks, or remains held across a switch.
namespace dispatch {
inline constexpr uint32_t NO_CPU = UINT32_MAX;
void enqueue(thread* owner) noexcept;
// Reserves an off-CPU thread, continues current, or reserves this CPU's idle.
thread* choose(cpu::per_cpu* pcpu) noexcept;
// A selected thread killed before execution still owns a claim during cleanup.
void release_candidate(thread* owner) noexcept;
void prepare_switch(cpu::per_cpu* pcpu, thread* next) noexcept;
// Called on the incoming stack, after outgoing SP/FPU/root are saved/switched.
// Last outgoing-thread access: publish off-CPU and requeue a runnable thread.
void finish_switch(cpu::per_cpu* pcpu) noexcept;
// Caller holds the thread-list lock, excluding fresh lookup pins/publication.
bool reclaimable(const thread* owner) noexcept;
// Bounded queue traversal under dispatch; useful for target and hosted stress.
bool queues_valid() noexcept;
} // namespace dispatch
} // namespace kernel::scheduler
