// SPDX-License-Identifier: MIT
// Production RCU with hosted CPU identity/allocation; IRQ masking stays a target check.
#include "test_harness.hpp"
#include <atomic>
#include <cstdlib>
#include <kernel/sync/rcu.hpp>
#include <thread>

namespace {
thread_local uint32_t current_cpu;
std::atomic<unsigned> allocations;
bool fail_allocation;

void reset(uint32_t cpus) {
    ASSERT_EQ(allocations.load(), 0U);
    current_cpu = 0;
    fail_allocation = false;
    kernel::cpu::g_cpu_count.store(cpus);
    for (uint32_t i = 0; i < cpus; ++i)
        kernel::cpu::g_percpu[i].cpu_id = i;
    kernel::rcu::init();
}

void quiesce(uint32_t cpu) {
    current_cpu = cpu;
    kernel::rcu::note_quiescent_state();
}

void increment(void* arg) {
    ++*static_cast<unsigned*>(arg);
}
} // namespace

namespace kernel::cpu {
per_cpu g_percpu[MAX_CPUS]{};
atomic<uint32_t> g_cpu_count{0};
per_cpu* this_cpu() noexcept {
    return &g_percpu[current_cpu];
}
} // namespace kernel::cpu

extern "C" void* kmalloc(size_t size) noexcept {
    if (fail_allocation) return nullptr;
    auto* result = std::malloc(size);
    if (result) ++allocations;
    return result;
}
extern "C" void kfree(void* pointer) noexcept {
    if (pointer) --allocations;
    std::free(pointer);
}

TEST(rcu_nested_reader_cannot_report_quiescence) {
    reset(2);
    unsigned reclaimed = 0;
    kernel::rcu::read_lock();
    kernel::rcu::read_lock();
    current_cpu = 1;
    ASSERT(kernel::rcu::call_rcu(increment, &reclaimed));
    quiesce(1);
    quiesce(0); // Even an explicit report cannot bypass an active reader.
    kernel::rcu::process_callbacks();
    ASSERT_EQ(reclaimed, 0U);
    kernel::rcu::read_unlock();
    kernel::rcu::process_callbacks();
    ASSERT_EQ(reclaimed, 0U);
    kernel::rcu::read_unlock();
    kernel::rcu::process_callbacks();
    ASSERT_EQ(reclaimed, 1U);
    ASSERT_EQ(allocations.load(), 0U);
}

TEST(rcu_requires_each_cpu_to_pass_the_callback_epoch) {
    reset(3);
    unsigned reclaimed = 0;
    quiesce(2); // A report predating the request cannot satisfy it.
    ASSERT(kernel::rcu::call_rcu(increment, &reclaimed));
    quiesce(0);
    quiesce(1);
    kernel::rcu::process_callbacks();
    ASSERT_EQ(reclaimed, 0U);
    quiesce(2);
    kernel::rcu::process_callbacks();
    kernel::rcu::process_callbacks();
    ASSERT_EQ(reclaimed, 1U);
    ASSERT_EQ(allocations.load(), 0U);
}

TEST(rcu_later_callback_needs_a_later_quiescent_report) {
    reset(2);
    unsigned first = 0, second = 0;
    ASSERT(kernel::rcu::call_rcu(increment, &first));
    quiesce(0);
    quiesce(1);
    ASSERT(kernel::rcu::call_rcu(increment, &second));
    kernel::rcu::process_callbacks();
    ASSERT_EQ(first, 1U);
    ASSERT_EQ(second, 0U);
    quiesce(0);
    quiesce(1);
    kernel::rcu::process_callbacks();
    ASSERT_EQ(second, 1U);
    ASSERT_EQ(allocations.load(), 0U);
}

TEST(rcu_synchronize_can_make_progress_without_a_local_switch) {
    reset(1);
    kernel::rcu::synchronize();
    kernel::rcu::synchronize();
}

TEST(rcu_callback_can_enqueue_another_callback) {
    reset(1);
    struct Chain {
        unsigned reclaimed;
        bool queued;
    } chain{};
    ASSERT(kernel::rcu::call_rcu(
        [](void* arg) {
            auto* state = static_cast<Chain*>(arg);
            ++state->reclaimed;
            state->queued = kernel::rcu::call_rcu(increment, &state->reclaimed);
        },
        &chain));
    quiesce(0);
    kernel::rcu::process_callbacks();
    ASSERT(chain.queued);
    ASSERT_EQ(chain.reclaimed, 1U);
    kernel::rcu::process_callbacks();
    ASSERT_EQ(chain.reclaimed, 1U);
    quiesce(0);
    kernel::rcu::process_callbacks();
    ASSERT_EQ(chain.reclaimed, 2U);
    ASSERT_EQ(allocations.load(), 0U);
}

TEST(rcu_allocation_failure_preserves_caller_ownership) {
    reset(1);
    unsigned reclaimed = 0;
    fail_allocation = true;
    ASSERT(!kernel::rcu::call_rcu(increment, &reclaimed));
    fail_allocation = false;
    ASSERT(!kernel::rcu::call_rcu(nullptr, &reclaimed));
    quiesce(0);
    kernel::rcu::process_callbacks();
    ASSERT_EQ(reclaimed, 0U);
    ASSERT_EQ(allocations.load(), 0U);
}

TEST(rcu_concurrent_updaters_do_not_reclaim_a_live_reader) {
    reset(3);
    unsigned reclaimed[2]{};
    std::atomic<bool> queued{true};
    kernel::rcu::read_lock();
    auto updater = [&](uint32_t cpu) {
        current_cpu = cpu;
        for (unsigned i = 0; i < 64; ++i) {
            if (!kernel::rcu::call_rcu(increment, &reclaimed[cpu - 1])) queued.store(false);
            kernel::rcu::note_quiescent_state();
            kernel::rcu::process_callbacks();
        }
    };
    std::thread first(updater, 1), second(updater, 2);
    first.join();
    second.join();
    ASSERT(queued.load());
    ASSERT_EQ(reclaimed[0] + reclaimed[1], 0U);
    kernel::rcu::read_unlock();
    quiesce(1);
    quiesce(2);
    kernel::rcu::process_callbacks();
    ASSERT_EQ(reclaimed[0], 64U);
    ASSERT_EQ(reclaimed[1], 64U);
    ASSERT_EQ(allocations.load(), 0U);
}

int main() {
    return RUN_ALL_TESTS("RCU grace periods and reclamation");
}
