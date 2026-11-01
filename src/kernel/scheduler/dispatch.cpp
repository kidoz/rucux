// SPDX-License-Identifier: MIT
#include <kernel/cpu/percpu.hpp>
#include <kernel/scheduler/dispatch.hpp>
#include <kernel/scheduler/scheduler.hpp>

namespace kernel::cpu {
void run_queue::init() noexcept {
    head = tail = nullptr;
    count = 0;
}
void run_queue::enqueue(scheduler::thread* t) noexcept {
    t->next = nullptr;
    if (tail)
        tail->next = t;
    else
        head = t;
    tail = t;
    ++count;
}
scheduler::thread* run_queue::dequeue() noexcept {
    auto* t = head;
    if (!t) return nullptr;
    head = t->next;
    if (!head) tail = nullptr;
    t->next = nullptr;
    --count;
    return t;
}
} // namespace kernel::cpu

namespace kernel::scheduler::dispatch {
namespace {
irq_spinlock dispatch_lock;

void queue_locked(thread* owner, cpu::per_cpu* pcpu) noexcept {
    if (owner->run_queued || owner->dispatch_cpu != NO_CPU || owner == pcpu->idle_thread) return;
    owner->last_cpu = pcpu->cpu_id;
    owner->run_queued = true;
    pcpu->queues[static_cast<uint8_t>(owner->priority)].enqueue(owner);
    ++pcpu->total_runnable;
}
thread* peek(cpu::per_cpu* pcpu) noexcept {
    for (auto& queue : pcpu->queues)
        if (queue.head) return queue.head;
    return nullptr;
}
thread* take(cpu::per_cpu* source, uint32_t destination) noexcept {
    for (auto& queue : source->queues) {
        if (auto* next = queue.dequeue()) {
            --source->total_runnable;
            next->run_queued = false;
            next->dispatch_cpu = destination;
            next->last_cpu = destination;
            return next;
        }
    }
    return nullptr;
}
} // namespace

void enqueue(thread* owner) noexcept {
    if (!owner) return;
    irq_lock_guard guard(dispatch_lock);
    const auto state = owner->state.load(acquire);
    if (state == thread_state::TERMINATED || state == thread_state::RUNNING) return;
    if (state == thread_state::BLOCKED) {
        auto expected = thread_state::BLOCKED;
        // Exit publication does not take dispatch. Never overwrite TERMINATED
        // if a wake races that publication on an executing/reserved thread.
        if (!owner->state.compare_exchange_strong(expected, thread_state::READY)) return;
    }
    uint32_t target = owner->last_cpu;
    if (target >= cpu::g_cpu_count.load(relaxed)) target = 0;
    // A wake before switch-out records READY only. The incoming stack will
    // enqueue it, preventing another CPU from entering the unfinished stack.
    queue_locked(owner, cpu::get_cpu(target));
}
thread* choose(cpu::per_cpu* pcpu) noexcept {
    irq_lock_guard guard(dispatch_lock);
    auto* current = pcpu->current_thread;
    auto* source = pcpu;
    auto* next = peek(source);
    if (!next) {
        uint32_t load = 0;
        const uint32_t count = cpu::g_cpu_count.load(acquire);
        for (uint32_t id = 0; id < count; ++id) {
            auto* other = cpu::get_cpu(id);
            if (other != pcpu && other->online && other->total_runnable > load) {
                source = other;
                load = other->total_runnable;
            }
        }
        next = peek(source);
    }
    if (current) {
        const auto state = current->state.load(acquire);
        if ((state == thread_state::RUNNING || state == thread_state::READY) &&
            (!next || current->priority < next->priority))
            return current;
    }
    if (next) return take(source, pcpu->cpu_id);
    auto* idle = pcpu->idle_thread;
    idle->dispatch_cpu = pcpu->cpu_id;
    return idle;
}
void release_candidate(thread* owner) noexcept {
    irq_lock_guard guard(dispatch_lock);
    owner->dispatch_cpu = NO_CPU;
}
void prepare_switch(cpu::per_cpu* pcpu, thread* next) noexcept {
    irq_lock_guard guard(dispatch_lock);
    auto* old = pcpu->current_thread;
    if (old != next) {
        if (old && old->state.load(acquire) == thread_state::RUNNING) old->state.store(thread_state::READY, release);
        pcpu->switching_from = old;
    }
    next->state.store(thread_state::RUNNING, release);
    next->last_cpu = pcpu->cpu_id;
    pcpu->current_thread = next;
}
void finish_switch(cpu::per_cpu* pcpu) noexcept {
    irq_lock_guard guard(dispatch_lock);
    auto* old = pcpu->switching_from;
    pcpu->switching_from = nullptr;
    if (!old) return;
    old->dispatch_cpu = NO_CPU;
    if (old->state.load(acquire) == thread_state::READY) queue_locked(old, pcpu);
    // Do not touch old after releasing dispatch: a remote reaper can free it.
}
bool reclaimable(const thread* owner) noexcept {
    irq_lock_guard guard(dispatch_lock);
    return owner->state.load(acquire) == thread_state::TERMINATED && owner->dispatch_cpu == NO_CPU &&
           !owner->run_queued && owner->pins.load(acquire) == 0;
}
uint32_t cpu_of(const thread* owner) noexcept {
    irq_lock_guard guard(dispatch_lock);
    return owner->last_cpu;
}
bool queues_valid() noexcept {
    irq_lock_guard guard(dispatch_lock);
    const uint32_t count = cpu::g_cpu_count.load(acquire);
    for (uint32_t id = 0; id < count; ++id) {
        const auto* pcpu = cpu::get_cpu(id);
        uint32_t total = 0;
        for (uint32_t priority = 0; priority < NUM_PRIOS; ++priority) {
            const auto& queue = pcpu->queues[priority];
            auto* node = queue.head;
            thread* last = nullptr;
            // A corrupt count must not make this diagnostic spin forever.
            if (queue.count > 65536) return false;
            for (uint32_t i = 0; i < queue.count; ++i) {
                if (!node || !node->run_queued || node->dispatch_cpu != NO_CPU || node->last_cpu != id ||
                    static_cast<uint32_t>(node->priority) != priority ||
                    node->state.load(acquire) != thread_state::READY)
                    return false;
                last = node;
                node = node->next;
            }
            if (node || last != queue.tail) return false;
            total += queue.count;
        }
        if (total != pcpu->total_runnable) return false;
    }
    return true;
}
} // namespace kernel::scheduler::dispatch
