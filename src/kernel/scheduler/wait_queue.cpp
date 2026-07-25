// SPDX-License-Identifier: MIT
#include <kernel/cpu/percpu.hpp>
#include <kernel/scheduler/wait_queue.hpp>

namespace kernel::scheduler {

bool wait_queue::wait(uint64_t wake_tick) noexcept {
    auto* cur = cpu::this_cpu()->current_thread;
    if (!cur) return false;

    {
        kernel::irq_lock_guard guard(m_lock);
        cur->wait_next = nullptr;
        if (!m_head) {
            m_head = cur;
            m_tail = cur;
        } else {
            m_tail->wait_next = cur;
            m_tail = cur;
        }
    }

    if (wake_tick > 0) {
        scheduler::sleep_until(wake_tick);
    } else {
        scheduler::block(thread_state::BLOCKED);
    }

    // Thread is awoken. Determine if we were woken up by wake_one/wake_all or by timeout.
    // If by timeout, we might still be in the queue.
    bool timed_out = false;
    {
        kernel::irq_lock_guard guard(m_lock);
        // Try to remove ourselves from the queue if we're still there
        thread* prev = nullptr;
        thread* curr = m_head;
        while (curr) {
            if (curr == cur) {
                timed_out = true;
                if (prev) {
                    prev->wait_next = curr->wait_next;
                } else {
                    m_head = curr->wait_next;
                }
                if (m_tail == curr) {
                    m_tail = prev;
                }
                break;
            }
            prev = curr;
            curr = curr->wait_next;
        }
        cur->wait_next = nullptr;
    }

    return !timed_out;
}

void wait_queue::wake_one() noexcept {
    thread* to_wake = nullptr;
    {
        kernel::irq_lock_guard guard(m_lock);
        if (m_head) {
            to_wake = m_head;
            m_head = m_head->wait_next;
            if (!m_head) m_tail = nullptr;
        }
    }

    if (to_wake) {
        to_wake->wait_next = nullptr;
        // The thread might be in the sleep queue, but unblock will set it READY
        // and add it to the runqueue. check_sleepers handles it properly.
        scheduler::unblock(to_wake);
    }
}

void wait_queue::wake_all() noexcept {
    thread* to_wake_head = nullptr;
    {
        kernel::irq_lock_guard guard(m_lock);
        to_wake_head = m_head;
        m_head = nullptr;
        m_tail = nullptr;
    }

    while (to_wake_head) {
        thread* t = to_wake_head;
        to_wake_head = t->wait_next;
        t->wait_next = nullptr;
        scheduler::unblock(t);
    }
}

} // namespace kernel::scheduler
