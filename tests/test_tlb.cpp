// SPDX-License-Identifier: MIT
#include "test_harness.hpp"
#include <atomic>
#include <kernel/memory/tlb.hpp>
#include <thread>

using namespace kernel::memory::tlb;
TEST(active_roots_track_join_leave_and_high_cpu_ids) {
    root_set roots;
    roots.enter(0, 0x1000);
    roots.enter(3, 0x1000);
    roots.enter(63, 0x1000);
    ASSERT_EQ(roots.mask(0x1000), (1ULL << 63) | 9);
    roots.enter(3, 0x2000);
    ASSERT_EQ(roots.mask(0x2000), 8ULL);
    ASSERT_EQ(roots.all(), (1ULL << 63) | 9);
    roots.enter(0, 0);
    roots.enter(63, 0);
    ASSERT_EQ(roots.mask(0x1000), 0ULL);
    ASSERT_EQ(roots.mask(0), 0ULL);
    ASSERT_EQ(roots.all(), 8ULL);
}
TEST(shootdown_acknowledgement_is_after_flush_and_allows_safe_frame_reuse) {
    mailbox slot;
    std::atomic<bool> entered{false}, allow{false};
    const auto sequence = slot.post(0x12345000);
    std::atomic<uintptr_t> observed{0};
    std::thread remote([&] {
        slot.service([&](uintptr_t address) {
            observed.store(address);
            entered.store(true);
            while (!allow.load())
                std::this_thread::yield();
        });
    });
    while (!entered.load())
        std::this_thread::yield();
    ASSERT(!slot.complete(sequence)); // Caller cannot release its old frame yet.
    allow.store(true);
    remote.join();
    ASSERT(slot.complete(sequence));
    ASSERT_EQ(observed.load(), 0x12345000UL);
    ASSERT(!slot.service([](uintptr_t) {}));
}
TEST(shootdown_service_never_takes_the_mapping_lock) {
    mailbox slot;
    kernel::irq_spinlock mapping;
    const auto flags = mapping.lock();
    const auto sequence = slot.post(0x4000);
    std::thread remote([&] { slot.service([](uintptr_t) {}); });
    // Models NMI/hardware broadcast while a target's normal code waits for a
    // lock with IRQs masked. Its acknowledgement path cannot acquire that lock.
    while (!slot.complete(sequence))
        std::this_thread::yield();
    mapping.unlock(flags);
    remote.join();
}
TEST(repeated_mailbox_reuse_keeps_payloads_and_acknowledgements_ordered) {
    mailbox slot;
    std::atomic<bool> done{false}, failed{false};
    std::thread remote([&] {
        uintptr_t expected = 1;
        while (!done.load()) {
            slot.service([&](uintptr_t address) {
                if (address != expected++) failed.store(true);
            });
            std::this_thread::yield();
        }
    });
    for (uintptr_t i = 1; i <= 10000; ++i) {
        const auto sequence = slot.post(i);
        while (!slot.complete(sequence))
            std::this_thread::yield();
    }
    done.store(true);
    remote.join();
    ASSERT(!failed.load());
}
int main() {
    return RUN_ALL_TESTS("Active roots and TLB completion protocol");
}
