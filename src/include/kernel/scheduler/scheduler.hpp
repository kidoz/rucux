// SPDX-License-Identifier: MIT
#pragma once
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::scheduler {

enum class thread_state { READY, RUNNING, BLOCKED, TERMINATED };

struct thread {
    uint32_t tid;
    thread_state state;
    uintptr_t stack_pointer;
    uintptr_t stack_base;
    size_t stack_size;
    uintptr_t pml4_phys;

    // IPC
    thread* send_queue_head;
    thread* send_queue_next;

    // For synchronous IPC, we need a place to store the message if we are blocked
    struct sync_message {
        uint32_t sender;
        uint32_t type;
        uint64_t data[4];
    } queued_msg;
    bool has_queued_msg;
    void* recv_buffer; // Pointer to the receiver's buffer in their address space

    // Async IPC
    static constexpr size_t ASYNC_QUEUE_SIZE = 16;
    struct message_entry {
        uint32_t sender;
        uint32_t type;
        uint64_t data[4];
    } async_queue[ASYNC_QUEUE_SIZE];
    size_t async_head;
    size_t async_tail;

    // For clone/threads
    void* user_entry;
    void* user_stack;
    void* user_arg;
    uintptr_t futex_wait_addr;

    // For round-robin or priority
    thread* next;
    thread* all_next;

    // File descriptors (POSIX)
    struct file_descriptor {
        void* node; // Actually vfs_node*, but we don't want to include vfs.hpp here to avoid circular dependencies
        size_t offset;
        int flags;
    };
    static constexpr size_t MAX_FDS = 32;
    file_descriptor fd_table[MAX_FDS];
};

class scheduler {
public:
    static void init() noexcept;
    static void schedule() noexcept;
    static thread* spawn(void (*entry)(), uint32_t tid) noexcept;
    static void add_thread(thread* t) noexcept;
    static void yield() noexcept;
    static void block(thread_state reason) noexcept;
    static void unblock(thread* t) noexcept;

    static thread* current_thread() noexcept;
    static thread* get_thread_by_tid(uint32_t tid) noexcept;

    static void wait_for_irq(uint8_t irq) noexcept;
    static void wake_irq_waiters(uint8_t irq) noexcept;

    // Multi-threading
    static long sys_clone(void* entry, void* stack, void* arg) noexcept;
    static long sys_futex(uint32_t* uaddr, int op, uint32_t val) noexcept;
};

} // namespace kernel::scheduler
