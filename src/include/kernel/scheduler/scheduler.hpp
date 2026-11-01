// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/process/credentials.hpp>
#include <kernel/sync/spinlock.hpp>
#include <lib/stddef.hpp>
#include <stdint.h>
#if defined(__x86_64__)
#include <arch/amd64/fpu.hpp>
#endif

namespace kernel::memory {
struct address_space;
}

namespace kernel::scheduler {

void idle_task() noexcept;

enum class thread_state { READY, RUNNING, BLOCKED, TERMINATED };
enum class ipc_wait_kind { none, legacy_send, legacy_receive, call, receive_call };

// Use an integer atomic so both GCC and Clang support the enum-valued state.
class atomic_thread_state {
public:
    thread_state load(memory_order order = acquire) const noexcept {
        return static_cast<thread_state>(value_.load(order));
    }
    void store(thread_state value, memory_order order = release) noexcept {
        value_.store(static_cast<uint32_t>(value), order);
    }

private:
    atomic<uint32_t> value_{static_cast<uint32_t>(thread_state::READY)};
};
static_assert(sizeof(atomic_thread_state) == sizeof(thread_state));

// Priority classes — lower number = higher priority
enum class thread_prio : uint8_t {
    RT = 0,     // Real-time (interrupt handlers, critical drivers)
    NORMAL = 1, // Normal user/kernel threads
    IDLE = 2,   // Idle priority (background work)
    NUM_PRIOS = 3
};

struct thread {
    uint32_t tid;
    atomic_thread_state state;
    thread_prio priority;
    uint32_t last_cpu; // CPU this thread last ran on (cache affinity)
    uintptr_t stack_pointer;
    uintptr_t stack_base;
    size_t stack_size;
    uintptr_t pml4_phys;
    memory::address_space* mm; // Shared with clone siblings. Null for kernel threads.
    process::credentials creds;

    // Incoming legacy/fixed request queue, selected by ipc_kind under IPC lock.
    thread* send_queue_head;
    thread* send_queue_next;

    struct sync_message {
        uint32_t sender;
        uint32_t type;
        uint64_t data[4];
    } queued_msg;
    bool has_queued_msg;
    void* recv_buffer; // Reserved legacy field; IPC never publishes stack buffers.

    // Fixed-word IPC: one accepted caller with an owned pin.
    thread* ipc_caller;   // Thread that called us via ipc_call (waiting for reply)
    uint64_t ipc_regs[4]; // Thread-owned type, d0, d1, d2 payload/reply.
    bool ipc_waiting;     // True if blocked in ipc_wait()

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
    uintptr_t user_saved_sp;
    uintptr_t user_saved_lr;
    uint32_t user_saved_spsr;
    uintptr_t futex_wait_addr;

    // Signals
    static constexpr int MAX_SIGNALS = 32;
    using sighandler_t = void (*)(int);
    sighandler_t sig_handlers[MAX_SIGNALS];   // SIG_DFL=0, SIG_IGN=1, or handler addr
    void (*sig_restorers[MAX_SIGNALS])(void); // Trampoline addresses
    atomic<uint32_t> sig_mask{0};             // Blocked signals bitmask
    atomic<uint32_t> sig_pending{0};          // Pending signals bitmask
    int exit_code;                            // Exit status (for pthread_join)
    bool exited;                              // True after thread has exited
#if defined(__x86_64__)
    arch::amd64::fpu_state fpu;
#endif
    uint32_t process_id; // Address-space owner, shared by cloned threads
    uint32_t parent_tid; // Parent/owner for waitpid-style reaping
    int32_t wait_target_tid;
    bool waiting_for_child;

    // Sleep
    uint64_t wake_tick;   // Tick when this thread should wake up
    thread* next_sleeper; // Next in sleep queue
    thread* wait_next;    // Next in wait queue

    // Scheduling links
    thread* next;     // Next in run queue
    thread* all_next; // Next in global thread list

    // File descriptors (POSIX) — dynamically allocated to save memory
    struct file_descriptor {
        void* node; // Actually vfs_node*, but we don't want to include vfs.hpp here to avoid circular dependencies
        size_t offset;
        int flags;
    };
    static constexpr size_t INITIAL_FDS = 8; // Start small, grow on demand
    static constexpr size_t MAX_FDS = 32;
    file_descriptor* fd_table; // Allocated on first use or inherited from parent
    size_t fd_count;           // Number of allocated slots in fd_table

    // Appended so existing assembly-visible thread offsets do not move.
    atomic<uint32_t> kernel_depth{0};
    irq_spinlock wait_lock;
    bool interruptible_wait = false; // Protected by wait_lock.

    // The thread list owns reclamation. Pins keep a peer object alive even
    // after exit; only the scheduler reaps it once all pins have been released.
    atomic<uint32_t> pins{0};
    atomic<bool> ipc_closed{false};
    ipc_wait_kind ipc_kind = ipc_wait_kind::none;
    thread* ipc_peer = nullptr; // Owned pin while sending/waiting for reply.
    thread* send_queue_tail = nullptr;
    size_t ipc_queue_count = 0;
    int ipc_result = 0;
    bool ipc_reply_cancelled = false; // Reply must acknowledge a cancelled call.
    void pin() noexcept { pins.fetch_add(1, relaxed); }
    void unpin() noexcept { pins.fetch_sub(1, acq_rel); }

    bool fatal_signal_pending() const noexcept {
        constexpr uint32_t KILL = 1U << 9U, TERM = 1U << 15U;
        const uint32_t mask = sig_mask.load(relaxed) & ~KILL;
        return (sig_pending.load(acquire) & (KILL | TERM) & ~mask) != 0;
    }
    // Caller holds its wait-queue lock. Publish BLOCKED under the same lock
    // used by signal posting, then release all locks before schedule().
    bool begin_interruptible_wait() noexcept {
        irq_lock_guard guard(wait_lock);
        if (fatal_signal_pending()) return false;
        interruptible_wait = true;
        state.store(thread_state::BLOCKED, release);
        return true;
    }
    void end_interruptible_wait() noexcept {
        irq_lock_guard guard(wait_lock);
        interruptible_wait = false;
    }

    // Ensure fd_table has at least `n` slots. Returns false on OOM.
    bool ensure_fd_capacity(size_t n) noexcept;
};

// Protects stack-owned buffers, reservations and references while the thread
// runs or sleeps inside a kernel operation. This does not disable preemption.
class kernel_scope {
public:
    explicit kernel_scope(thread* owner) noexcept
        : owner_(owner) {
        if (owner_) owner_->kernel_depth.fetch_add(1, acq_rel);
    }
    ~kernel_scope() noexcept {
        if (owner_) owner_->kernel_depth.fetch_sub(1, acq_rel);
    }
    kernel_scope(const kernel_scope&) = delete;
    kernel_scope& operator=(const kernel_scope&) = delete;

private:
    thread* owner_;
};

class scheduler {
public:
    static void init() noexcept;
    static void schedule() noexcept;
    static thread* spawn(void (*entry)(), uint32_t tid) noexcept;
    static thread* spawn_user(uintptr_t pml4_phys, void* entry, void* stack, void* arg, uint32_t tid = 0,
                              bool enqueue = true) noexcept;
    static void add_thread(thread* t) noexcept;
    static void yield() noexcept;
    static void block(thread_state reason) noexcept;
    static void unblock(thread* t) noexcept;
    static void sleep_until(uint64_t tick) noexcept;
    static void check_sleepers(uint64_t current_tick) noexcept;
    static void exit(int status = 0) noexcept;
    static void cleanup_terminated() noexcept;
    // Stops a current thread already marked TERMINATED; never returns on target.
    static void finish_termination() noexcept;
    // Holds the thread-list lock through lookup and signal posting.
    static int signal_thread(int pid, int sig) noexcept;

    // Syscalls
    static int sys_top(void* buffer, size_t size) noexcept;
    static long sys_waitpid(int pid, int* wstatus, int options) noexcept;

    static thread* current_thread() noexcept;
    static thread* get_thread_by_tid(uint32_t tid) noexcept;
    // Returns one pin on a live IPC peer, or nullptr; caller must unpin().
    static thread* pin_thread(uint32_t tid) noexcept;

    static void wait_for_irq(uint8_t irq) noexcept;
    static void wake_irq_waiters(uint8_t irq) noexcept;

    // Multi-threading
    static long sys_clone(void* entry, void* stack, void* arg) noexcept;
    static long sys_futex(uint32_t* uaddr, int op, uint32_t val) noexcept;
};

} // namespace kernel::scheduler
