// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/sync/spinlock.hpp>

namespace kernel::scheduler {

class wait_queue {
public:
    wait_queue() = default;
    ~wait_queue() = default;

    // Block the current thread on this queue until woke up.
    // Optionally accepts a tick deadline (wake_tick).
    // Returns true if awoken normally (wake_one/wake_all), false if timeout.
    bool wait(uint64_t wake_tick = 0) noexcept;

    // Wake up one blocked thread (FIFO).
    void wake_one() noexcept;

    // Wake up all blocked threads.
    void wake_all() noexcept;

private:
    kernel::irq_spinlock m_lock;
    thread* m_head = nullptr;
    thread* m_tail = nullptr;
};

} // namespace kernel::scheduler
