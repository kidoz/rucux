// SPDX-License-Identifier: MIT
#pragma once
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::scheduler {

void idle_task() noexcept;

enum class thread_state { READY, RUNNING, BLOCKED, TERMINATED };

// Priority classes — lower number = higher priority
enum class thread_prio : uint8_t {
    RT     = 0,   // Real-time (interrupt handlers, critical drivers)
    NORMAL = 1,   // Normal user/kernel threads
    IDLE   = 2,   // Idle priority (background work)
    NUM_PRIOS = 3
};

struct thread {
    uint32_t tid;
    thread_state state;
    thread_prio priority;
    uint32_t last_cpu;         // CPU this thread last ran on (cache affinity)
    uintptr_t stack_pointer;
    uintptr_t stack_base;
    size_t stack_size;
    uintptr_t pml4_phys;

    // IPC (legacy)
    thread* send_queue_head;
    thread* send_queue_next;

    struct sync_message {
        uint32_t sender;
        uint32_t type;
        uint64_t data[4];
    } queued_msg;
    bool has_queued_msg;
    void* recv_buffer;

    // Fast IPC: direct thread-to-thread register transfer
    thread* ipc_caller;    // Thread that called us via ipc_call (waiting for reply)
    uint64_t ipc_regs[4];  // type, d0, d1, d2 — transferred in registers
    bool ipc_waiting;       // True if blocked in ipc_wait()

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

    // Signals
    static constexpr int MAX_SIGNALS = 32;
    using sighandler_t = void (*)(int);
    sighandler_t sig_handlers[MAX_SIGNALS]; // SIG_DFL=0, SIG_IGN=1, or handler addr
    uint32_t sig_mask;      // Blocked signals bitmask
    uint32_t sig_pending;   // Pending signals bitmask
    int exit_code;          // Exit status (for pthread_join)
    bool exited;            // True after thread has exited

    // Scheduling links
    thread* next;     // Next in run queue
    thread* all_next; // Next in global thread list

    // File descriptors (POSIX) — dynamically allocated to save memory
    struct file_descriptor {
        void* node; // Actually vfs_node*, but we don't want to include vfs.hpp here to avoid circular dependencies
        size_t offset;
        int flags;
    };
    static constexpr size_t INITIAL_FDS = 8;  // Start small, grow on demand
    static constexpr size_t MAX_FDS = 32;
    file_descriptor* fd_table; // Allocated on first use or inherited from parent
    size_t fd_count;           // Number of allocated slots in fd_table

    // Ensure fd_table has at least `n` slots. Returns false on OOM.
    bool ensure_fd_capacity(size_t n) noexcept;
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
    static void exit() noexcept;
    static void cleanup_terminated() noexcept;

    static thread* current_thread() noexcept;
    static thread* get_thread_by_tid(uint32_t tid) noexcept;

    static void wait_for_irq(uint8_t irq) noexcept;
    static void wake_irq_waiters(uint8_t irq) noexcept;

    // Multi-threading
    static long sys_clone(void* entry, void* stack, void* arg) noexcept;
    static long sys_futex(uint32_t* uaddr, int op, uint32_t val) noexcept;
};

} // namespace kernel::scheduler
