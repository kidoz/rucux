// SPDX-License-Identifier: MIT
// Host-compiled test for kernel::spinlock

#include "test_harness.hpp"
#include "../src/include/kernel/sync/spinlock.hpp"
#include <thread>
#include <vector>

TEST(spinlock_basic_lock_unlock) {
    kernel::spinlock lock;
    lock.lock();
    ASSERT(lock.is_locked());
    lock.unlock();
    ASSERT(!lock.is_locked());
}

TEST(spinlock_try_lock_success) {
    kernel::spinlock lock;
    ASSERT(lock.try_lock());
    ASSERT(lock.is_locked());
    lock.unlock();
}

TEST(spinlock_try_lock_failure) {
    kernel::spinlock lock;
    lock.lock();
    ASSERT(!lock.try_lock()); // Already locked
    lock.unlock();
}

TEST(spinlock_fairness) {
    // Verify ticket spinlock FIFO: t1 acquires before t2 if t1 called lock() first.
    kernel::spinlock lock;
    int order[2] = {0, 0};
    int next = 0;

    lock.lock(); // Hold initially

    std::thread t1([&]() {
        lock.lock();
        order[next++] = 1;
        lock.unlock();
    });

    for (int i = 0; i < 100000; i++) kernel::cpu_relax();

    std::thread t2([&]() {
        lock.lock();
        order[next++] = 2;
        lock.unlock();
    });

    for (int i = 0; i < 100000; i++) kernel::cpu_relax();

    lock.unlock(); // Release — t1 then t2

    t1.join();
    t2.join();

    ASSERT_EQ(order[0], 1);
    ASSERT_EQ(order[1], 2);
}

TEST(spinlock_concurrent_counter) {
    kernel::spinlock lock;
    int counter = 0;
    constexpr int N_THREADS = 8;
    constexpr int ITERS = 100000;

    std::vector<std::thread> threads;
    for (int i = 0; i < N_THREADS; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < ITERS; ++j) {
                lock.lock();
                counter++;
                lock.unlock();
            }
        });
    }

    for (auto& t : threads) t.join();
    ASSERT_EQ(counter, N_THREADS * ITERS);
}

TEST(irq_spinlock_basic) {
    kernel::irq_spinlock lock;
    uintptr_t flags = lock.lock();
    lock.unlock(flags);
    ASSERT(true);
}

TEST(irq_lock_guard_scoped) {
    kernel::irq_spinlock lock;
    {
        kernel::irq_lock_guard guard(lock);
    }
    // Verify lock is released by acquiring again
    uintptr_t flags = lock.lock();
    lock.unlock(flags);
    ASSERT(true);
}

int main() {
    return RUN_ALL_TESTS("Spinlock Tests");
}
