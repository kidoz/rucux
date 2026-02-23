// SPDX-License-Identifier: MIT
#include <kernel/ipc/ipc.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <lib/string.hpp>

namespace kernel::ipc {

// using namespace scheduler;

void ipc_manager::send_sync(uint32_t target_tid, const message& msg) noexcept {
    scheduler::thread* current = scheduler::scheduler::current_thread();
    scheduler::thread* target = scheduler::scheduler::get_thread_by_tid(target_tid);

    if (!target) {
        kernel::print("IPC: Target {} not found\n", target_tid);
        return;
    }

    // Queue the message inside the sender's kernel thread struct
    current->queued_msg.sender = current->tid; // Enforce correct sender!
    current->queued_msg.type = msg.type;
    lib::memcpy(current->queued_msg.data, msg.data, sizeof(msg.data));
    current->has_queued_msg = true;
    current->send_queue_next = nullptr;

    // Add sender to target's send_queue
    if (!target->send_queue_head) {
        target->send_queue_head = current;
    } else {
        scheduler::thread* cur = target->send_queue_head;
        while (cur->send_queue_next)
            cur = cur->send_queue_next;
        cur->send_queue_next = current;
    }

    // Check if receiver is waiting
    if (target->state == scheduler::thread_state::BLOCKED && target->recv_buffer) {
        scheduler::scheduler::unblock(target);
    }

    scheduler::scheduler::block(scheduler::thread_state::BLOCKED);
}

void ipc_manager::send_async(uint32_t target_tid, const message& msg) noexcept {
    scheduler::thread* target = scheduler::scheduler::get_thread_by_tid(target_tid);
    if (!target) return;

    // Add to circular buffer
    size_t next_tail = (target->async_tail + 1) % scheduler::thread::ASYNC_QUEUE_SIZE;
    if (next_tail == target->async_head) {
        // Queue full!
        return;
    }

    auto& entry = target->async_queue[target->async_tail];
    entry.sender = scheduler::scheduler::current_thread()->tid;
    entry.type = msg.type;
    lib::memcpy(entry.data, msg.data, sizeof(msg.data));

    target->async_tail = next_tail;

    // Wake up receiver if it's waiting for async messages
    if (target->state == scheduler::thread_state::BLOCKED && target->recv_buffer == nullptr) {
        scheduler::scheduler::unblock(target);
    }
}

void ipc_manager::receive_sync(message& msg) noexcept {
    scheduler::thread* current = scheduler::scheduler::current_thread();

    // 1. Check for async messages first
    if (current->async_head != current->async_tail) {
        auto& entry = current->async_queue[current->async_head];
        msg.sender = entry.sender;
        msg.type = entry.type;
        lib::memcpy(msg.data, entry.data, sizeof(msg.data));

        current->async_head = (current->async_head + 1) % scheduler::thread::ASYNC_QUEUE_SIZE;
        return;
    }

    // 2. Check for waiting sync senders
    if (current->send_queue_head) {
        scheduler::thread* sender = current->send_queue_head;
        current->send_queue_head = sender->send_queue_next;

        // Copy message from sender's kernel buffer to receiver's user buffer
        msg.sender = sender->queued_msg.sender;
        msg.type = sender->queued_msg.type;
        lib::memcpy(msg.data, sender->queued_msg.data, sizeof(msg.data));

        sender->has_queued_msg = false;
        scheduler::scheduler::unblock(sender);
        return;
    }

    // Block receiver
    current->recv_buffer = &msg;
    scheduler::scheduler::block(scheduler::thread_state::BLOCKED);

    // When we wake up, a sender has queued a message and added themselves to send_queue_head.
    // The message is NOT copied yet! We must copy it now that we are running in our CR3.
    if (current->send_queue_head) {
        scheduler::thread* sender = current->send_queue_head;
        current->send_queue_head = sender->send_queue_next;

        msg.sender = sender->queued_msg.sender;
        msg.type = sender->queued_msg.type;
        lib::memcpy(msg.data, sender->queued_msg.data, sizeof(msg.data));

        sender->has_queued_msg = false;
        scheduler::scheduler::unblock(sender);
    }
    current->recv_buffer = nullptr;
}

} // namespace kernel::ipc
