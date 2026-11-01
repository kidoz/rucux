// SPDX-License-Identifier: MIT
#include "test_harness.hpp"
#include <array>
#include <atomic>
#include <kernel/cpu/percpu.hpp>
#include <kernel/scheduler/dispatch.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <memory>
#include <new>
#include <thread>

namespace kernel::cpu {
per_cpu g_percpu[MAX_CPUS]{};
atomic<uint32_t> g_cpu_count{4};
} // namespace kernel::cpu
namespace {
using namespace kernel;
using namespace scheduler;
std::array<thread, 32> tasks;
std::array<thread, 4> idles;
void reset() {
    for (uint32_t i = 0; i < 4; ++i) {
        new (&cpu::g_percpu[i]) cpu::per_cpu{};
        new (&idles[i]) thread{};
        idles[i].priority = thread_prio::IDLE;
        auto* pcpu = cpu::get_cpu(i);
        pcpu->cpu_id = i;
        pcpu->online = true;
        pcpu->idle_thread = &idles[i];
    }
    for (uint32_t i = 0; i < tasks.size(); ++i) {
        new (&tasks[i]) thread{};
        tasks[i].tid = i + 1;
        tasks[i].priority = thread_prio::NORMAL;
    }
}
thread* enter(uint32_t id) {
    auto* pcpu = cpu::get_cpu(id);
    auto* next = dispatch::choose(pcpu);
    dispatch::prepare_switch(pcpu, next);
    dispatch::finish_switch(pcpu);
    return next;
}
} // namespace
TEST(terminated_current_is_retained_until_incoming_stack_finishes_switch) {
    reset();
    auto owner = std::make_unique<thread>();
    owner->priority = thread_prio::NORMAL;
    dispatch::enqueue(owner.get());
    ASSERT(enter(0) == owner.get());
    owner->state.store(thread_state::TERMINATED);
    ASSERT(!dispatch::reclaimable(owner.get()));
    auto* next = dispatch::choose(cpu::get_cpu(0));
    ASSERT(next == &idles[0]);
    dispatch::prepare_switch(cpu::get_cpu(0), next);
    ASSERT(cpu::get_cpu(0)->current_thread == next);
    ASSERT(!dispatch::reclaimable(owner.get()));
    dispatch::finish_switch(cpu::get_cpu(0));
    ASSERT(dispatch::reclaimable(owner.get()));
    owner.reset();
    dispatch::finish_switch(cpu::get_cpu(0)); // Must not retain the freed pointer.
}
TEST(selected_fatal_candidate_is_retained_through_exit_callbacks) {
    reset();
    auto& owner = tasks[0];
    dispatch::enqueue(&owner);
    ASSERT(dispatch::choose(cpu::get_cpu(1)) == &owner);
    owner.state.store(thread_state::TERMINATED);
    ASSERT(!dispatch::reclaimable(&owner));
    dispatch::release_candidate(&owner);
    ASSERT(dispatch::reclaimable(&owner));
    dispatch::enqueue(&owner);
    ASSERT_EQ(cpu::get_cpu(0)->total_runnable, 0U);
    ASSERT(owner.state.load() == thread_state::TERMINATED);
}
TEST(peer_pin_defers_reaping_after_cpu_handoff) {
    reset();
    auto& owner = tasks[0];
    dispatch::enqueue(&owner);
    ASSERT(enter(0) == &owner);
    owner.pin();
    owner.state.store(thread_state::TERMINATED);
    enter(0);
    ASSERT(!dispatch::reclaimable(&owner));
    owner.unpin();
    ASSERT(dispatch::reclaimable(&owner));
}
TEST(early_wake_cannot_expose_an_executing_stack_to_work_stealing) {
    reset();
    auto& owner = tasks[0];
    dispatch::enqueue(&owner);
    ASSERT(enter(0) == &owner);
    owner.state.store(thread_state::BLOCKED);
    dispatch::enqueue(&owner);
    ASSERT(owner.state.load() == thread_state::READY);
    ASSERT(!owner.run_queued);
    ASSERT(enter(1) == &idles[1]);
    ASSERT(enter(0) == &owner); // Early wake requires no switch at all.
    ASSERT(!owner.run_queued);
}
TEST(wake_during_switch_enqueues_once_only_after_incoming_tail) {
    reset();
    auto& owner = tasks[0];
    dispatch::enqueue(&owner);
    ASSERT(enter(0) == &owner);
    owner.state.store(thread_state::BLOCKED);
    auto* next = dispatch::choose(cpu::get_cpu(0));
    dispatch::prepare_switch(cpu::get_cpu(0), next);
    for (unsigned i = 0; i < 100; ++i)
        dispatch::enqueue(&owner);
    ASSERT_EQ(cpu::get_cpu(0)->total_runnable, 0U);
    ASSERT(enter(1) == &idles[1]);
    dispatch::finish_switch(cpu::get_cpu(0));
    ASSERT_EQ(cpu::get_cpu(0)->total_runnable, 1U);
    ASSERT(enter(1) == &owner);
    ASSERT_EQ(cpu::get_cpu(0)->total_runnable, 0U);
    ASSERT(dispatch::queues_valid());
}
TEST(late_wake_enqueues_once_and_dead_thread_is_never_resurrected) {
    reset();
    auto& owner = tasks[0];
    dispatch::enqueue(&owner);
    ASSERT(enter(0) == &owner);
    owner.state.store(thread_state::BLOCKED);
    enter(0);
    ASSERT(!owner.run_queued);
    dispatch::enqueue(&owner);
    dispatch::enqueue(&owner);
    ASSERT_EQ(cpu::get_cpu(0)->total_runnable, 1U);
    ASSERT(enter(0) == &owner);
    owner.state.store(thread_state::TERMINATED);
    enter(0);
    dispatch::enqueue(&owner);
    ASSERT(dispatch::reclaimable(&owner));
    ASSERT_EQ(cpu::get_cpu(0)->total_runnable, 0U);
}
TEST(concurrent_wakes_cannot_duplicate_an_off_cpu_queue_entry) {
    reset();
    std::array<std::thread, 4> workers;
    for (auto& worker : workers)
        worker = std::thread([] {
            for (unsigned i = 0; i < 1000; ++i)
                dispatch::enqueue(&tasks[0]);
        });
    for (auto& worker : workers)
        worker.join();
    ASSERT_EQ(cpu::get_cpu(0)->total_runnable, 1U);
    ASSERT(dispatch::queues_valid());
}
TEST(round_robin_requeues_after_switch_and_preserves_priority) {
    reset();
    for (unsigned i = 0; i < 3; ++i)
        dispatch::enqueue(&tasks[i]);
    ASSERT(enter(0) == &tasks[0]);
    ASSERT(enter(0) == &tasks[1]);
    ASSERT(enter(0) == &tasks[2]);
    ASSERT(enter(0) == &tasks[0]);
    tasks[3].priority = thread_prio::RT;
    dispatch::enqueue(&tasks[3]);
    ASSERT(enter(0) == &tasks[3]);
    ASSERT(enter(0) == &tasks[3]);
    ASSERT(dispatch::queues_valid());
}
TEST(concurrent_cpus_wakes_and_reaping_checks_keep_exclusive_stack_ownership) {
    reset();
    std::array<std::atomic<unsigned>, 32> active{};
    std::atomic<bool> failed{false}, done{false};
    for (auto& owner : tasks)
        dispatch::enqueue(&owner);
    std::thread waker([&] {
        while (!done.load())
            for (auto& owner : tasks)
                dispatch::enqueue(&owner);
    });
    std::thread reaper([&] {
        while (!done.load()) {
            for (unsigned i = 0; i < tasks.size(); ++i)
                if (dispatch::reclaimable(&tasks[i]) && active[i].load() != 0) failed.store(true);
            if (!dispatch::queues_valid()) failed.store(true);
        }
    });
    std::array<std::thread, 4> cpus;
    for (unsigned id = 0; id < cpus.size(); ++id) {
        cpus[id] = std::thread([&, id] {
            auto* pcpu = cpu::get_cpu(id);
            for (unsigned step = 0; step < 2000; ++step) {
                auto* old = pcpu->current_thread;
                if (old && old->tid) {
                    if (step % 7 == 0)
                        old->state.store(thread_state::TERMINATED);
                    else if (step % 3 == 0)
                        old->state.store(thread_state::BLOCKED);
                }
                auto* next = dispatch::choose(pcpu);
                if (old != next) {
                    if (old && old->tid) active[old->tid - 1].fetch_sub(1);
                    if (next->tid && active[next->tid - 1].fetch_add(1) != 0) failed.store(true);
                }
                dispatch::prepare_switch(pcpu, next);
                std::this_thread::yield();
                dispatch::finish_switch(pcpu);
            }
        });
    }
    for (auto& worker : cpus)
        worker.join();
    done.store(true);
    waker.join();
    reaper.join();
    ASSERT(!failed.load());
    ASSERT(dispatch::queues_valid());
    for (auto& owner : tasks) {
        ASSERT(owner.state.load() == thread_state::TERMINATED);
        ASSERT(dispatch::reclaimable(&owner));
    }
}
int main() {
    return RUN_ALL_TESTS("SMP dispatch ownership and thread retirement");
}
