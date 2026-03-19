// SPDX-License-Identifier: MIT
#include <kernel/cpu/percpu.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/sync/spinlock.hpp>
#include <knew.hpp>

#ifdef __x86_64__
#include <arch/amd64/tss.hpp>
#endif

namespace kernel::scheduler {

// Global ready queue — protected by g_ready_lock.
// Will move to per-CPU run queues in the SMP scheduler phase.
static thread* g_ready_queue = nullptr;
static thread* g_ready_tail = nullptr;
static kernel::irq_spinlock g_ready_lock;

// Global thread list — protected by g_threads_lock
static thread* g_all_threads = nullptr;
static kernel::irq_spinlock g_threads_lock;

static thread* g_irq_waiters[256] = {nullptr};

extern "C" void switch_context(uintptr_t* old_stack, uintptr_t new_stack) noexcept;
extern "C" {
// Kept as globals for assembly (switch.S syscall_entry).
// Synced from per-CPU on every context switch.
uintptr_t g_current_kernel_stack = 0;
uintptr_t g_temp_user_rsp = 0;
void jump_to_user_space(void* entry, void* stack, void* arg);
}

bool thread::ensure_fd_capacity(size_t n) noexcept {
    if (n > MAX_FDS) n = MAX_FDS;
    if (fd_table && fd_count >= n) return true;

    size_t new_count = fd_count ? fd_count : INITIAL_FDS;
    while (new_count < n) new_count *= 2;
    if (new_count > MAX_FDS) new_count = MAX_FDS;

    auto* new_table = new file_descriptor[new_count];
    if (!new_table) return false;

    // Copy old entries
    for (size_t i = 0; i < fd_count; ++i)
        new_table[i] = fd_table[i];
    // Zero new entries
    for (size_t i = fd_count; i < new_count; ++i) {
        new_table[i].node = nullptr;
        new_table[i].offset = 0;
        new_table[i].flags = 0;
    }

    delete[] fd_table;
    fd_table = new_table;
    fd_count = new_count;
    return true;
}

static void clone_trampoline() {
    thread* t = scheduler::scheduler::current_thread();
    jump_to_user_space(t->user_entry, t->user_stack, t->user_arg);
}

static kernel::atomic<uint32_t> g_next_tid{1000};

long scheduler::sys_clone(void* entry, void* stack, void* arg) noexcept {
    thread* parent = current_thread();
    if (!parent) return -1;

    thread* t = new thread();
    t->tid = g_next_tid.fetch_add(1, kernel::relaxed) + 1;
    t->stack_size = 8192;
    t->stack_base = reinterpret_cast<uintptr_t>(new uint8_t[t->stack_size]);
    t->pml4_phys = parent->pml4_phys; // Share address space

    // Copy FDs from parent
    t->fd_table = nullptr;
    t->fd_count = 0;
    if (parent->fd_table && parent->fd_count > 0) {
        t->ensure_fd_capacity(parent->fd_count);
        for (size_t i = 0; i < t->fd_count && i < parent->fd_count; ++i)
            t->fd_table[i] = parent->fd_table[i];
    }

    t->async_head = 0;
    t->async_tail = 0;
    t->send_queue_head = nullptr;
    t->send_queue_next = nullptr;
    t->has_queued_msg = false;
    t->recv_buffer = nullptr;

    t->user_entry = entry;
    t->user_stack = stack;
    t->user_arg = arg;
    t->futex_wait_addr = 0;

    uint64_t* kstack = reinterpret_cast<uint64_t*>(t->stack_base + t->stack_size);
    *(--kstack) = reinterpret_cast<uint64_t>(clone_trampoline);
    *(--kstack) = 0; // rbp
    *(--kstack) = 0; // rbx
    *(--kstack) = 0; // r12
    *(--kstack) = 0; // r13
    *(--kstack) = 0; // r14
    *(--kstack) = 0; // r15

    t->stack_pointer = reinterpret_cast<uintptr_t>(kstack);

    {
        kernel::irq_lock_guard guard(g_threads_lock);
        t->all_next = g_all_threads;
        g_all_threads = t;
    }

    add_thread(t);
    return t->tid;
}

long scheduler::sys_futex(uint32_t* uaddr, int op, uint32_t val) noexcept {
    thread* parent = current_thread();
    if (!parent) return -1;

    if (op == 0) { // WAIT
        if (*uaddr == val) {
            parent->futex_wait_addr = reinterpret_cast<uintptr_t>(uaddr);
            scheduler::block(thread_state::BLOCKED);
            return 0;
        } else {
            return -1; // Value mismatch
        }
    } else if (op == 1) { // WAKE
        int woke = 0;
        kernel::irq_lock_guard guard(g_threads_lock);
        for (thread* cur = g_all_threads; cur; cur = cur->all_next) {
            if (cur->pml4_phys == parent->pml4_phys && cur->state == thread_state::BLOCKED &&
                cur->futex_wait_addr == reinterpret_cast<uintptr_t>(uaddr)) {

                cur->futex_wait_addr = 0;
                scheduler::unblock(cur);
                woke++;
                if (woke == (int)val) break;
            }
        }
        return woke;
    }
    return -1;
}

void idle_task() {
    while (true) {
#if defined(__x86_64__)
        asm volatile("sti; hlt");
#elif defined(__arm__)
        asm volatile("wfi");
#endif
    }
}

void scheduler::init() noexcept {
    kernel::print("Scheduler initialized\n");

    auto* cpu = cpu::this_cpu();

    // Create per-CPU idle thread
    auto* idle = new thread();
    idle->tid = 0;
    idle->stack_size = 4096;
    idle->stack_base = reinterpret_cast<uintptr_t>(new uint8_t[idle->stack_size]);
    idle->pml4_phys = 0; // Kernel CR3

#if defined(__x86_64__)
    uint64_t* stack = reinterpret_cast<uint64_t*>(idle->stack_base + idle->stack_size);
    *(--stack) = reinterpret_cast<uint64_t>(idle_task);
    *(--stack) = 0; // rbp
    *(--stack) = 0; // rbx
    *(--stack) = 0; // r12
    *(--stack) = 0; // r13
    *(--stack) = 0; // r14
    *(--stack) = 0; // r15
    idle->stack_pointer = reinterpret_cast<uintptr_t>(stack);
#elif defined(__arm__)
    uint32_t* stack = reinterpret_cast<uint32_t*>(idle->stack_base + idle->stack_size);
    *(--stack) = reinterpret_cast<uint32_t>(idle_task); // pc (via pop {r4-r11, pc})
    for (int i = 0; i < 8; ++i) *(--stack) = 0;       // r4-r11
    idle->stack_pointer = reinterpret_cast<uintptr_t>(stack);
#endif
    idle->state = thread_state::READY;

    cpu->idle_thread = idle;
    cpu->current_thread = nullptr;
}

thread* scheduler::spawn(void (*entry)(), uint32_t tid) noexcept {
    thread* t = new thread();
    t->tid = tid;
    t->stack_size = 8192; // 8 KB
    t->stack_base = reinterpret_cast<uintptr_t>(new uint8_t[t->stack_size]);
    t->pml4_phys = 0;

    // Initial stack setup: simulate a context switch save frame
    // For amd64, this would be r15, r14, r13, r12, rbx, rbp, ret_addr
    uint64_t* stack = reinterpret_cast<uint64_t*>(t->stack_base + t->stack_size);
    *(--stack) = reinterpret_cast<uint64_t>(entry); // return address for switch_context 'ret'
    *(--stack) = 0;                                 // rbp
    *(--stack) = 0;                                 // rbx
    *(--stack) = 0;                                 // r12
    *(--stack) = 0;                                 // r13
    *(--stack) = 0;                                 // r14
    *(--stack) = 0;                                 // r15

    t->stack_pointer = reinterpret_cast<uintptr_t>(stack);
    t->async_head = 0;
    t->async_tail = 0;
    t->send_queue_head = nullptr;
    t->send_queue_next = nullptr;
    t->has_queued_msg = false;
    t->recv_buffer = nullptr;

    t->fd_table = nullptr;
    t->fd_count = 0;

    {
        kernel::irq_lock_guard guard(g_threads_lock);
        t->all_next = g_all_threads;
        g_all_threads = t;
    }

    add_thread(t);
    return t;
}

void scheduler::add_thread(thread* t) noexcept {
    t->state = thread_state::READY;
    t->next = nullptr;

    kernel::irq_lock_guard guard(g_ready_lock);
    if (!g_ready_queue) {
        g_ready_queue = t;
        g_ready_tail = t;
    } else {
        g_ready_tail->next = t;
        g_ready_tail = t;
    }
}

void scheduler::cleanup_terminated() noexcept {
    auto* this_thread = cpu::this_cpu()->current_thread;

    kernel::irq_lock_guard guard(g_threads_lock);
    thread* prev = nullptr;
    thread* cur = g_all_threads;
    while (cur) {
        if (cur->state == thread_state::TERMINATED && cur != this_thread) {
            thread* to_delete = cur;
            if (prev) {
                prev->all_next = cur->all_next;
            } else {
                g_all_threads = cur->all_next;
            }
            cur = cur->all_next;

            delete[] reinterpret_cast<uint8_t*>(to_delete->stack_base);
            delete to_delete;
        } else {
            prev = cur;
            cur = cur->all_next;
        }
    }
}

void scheduler::exit() noexcept {
    auto* cur = cpu::this_cpu()->current_thread;
    if (!cur) return;
    cur->state = thread_state::TERMINATED;
    schedule();
    while (true) {
#if defined(__x86_64__)
        asm volatile("hlt");
#elif defined(__arm__)
        asm volatile("wfi");
#endif
    }
}

void scheduler::schedule() noexcept {
    // Disable interrupts for the entire scheduling decision + context switch.
    // This prevents re-entrant schedule() calls from timer IRQs between
    // updating thread state and completing the switch.
    uintptr_t flags = kernel::irq_save();

    auto* pcpu = cpu::this_cpu();

    cleanup_terminated();

    thread* old_thread = pcpu->current_thread;
    thread* new_thread = nullptr;

    {
        uintptr_t rq_flags = g_ready_lock.lock();

        if (!g_ready_queue) {
            if (old_thread &&
                (old_thread->state == thread_state::RUNNING || old_thread->state == thread_state::READY)) {
                g_ready_lock.unlock(rq_flags);
                kernel::irq_restore(flags);
                return;
            }
            new_thread = pcpu->idle_thread;
        } else {
            new_thread = g_ready_queue;
            g_ready_queue = new_thread->next;
            new_thread->next = nullptr;

            if (g_ready_tail == new_thread) {
                g_ready_tail = g_ready_queue;
            } else if (!g_ready_queue) {
                g_ready_tail = nullptr;
            }
        }

        if (old_thread && old_thread->state == thread_state::RUNNING && old_thread != pcpu->idle_thread) {
            old_thread->state = thread_state::READY;
            old_thread->next = nullptr;
            if (!g_ready_queue) {
                g_ready_queue = old_thread;
                g_ready_tail = old_thread;
            } else {
                g_ready_tail->next = old_thread;
                g_ready_tail = old_thread;
            }
        }

        g_ready_lock.unlock(rq_flags);
    }

    new_thread->state = thread_state::RUNNING;
    pcpu->current_thread = new_thread;

    uintptr_t new_kstack = new_thread->stack_base + new_thread->stack_size;
    pcpu->kernel_stack = new_kstack;
    g_current_kernel_stack = new_kstack; // Sync for assembly

    if (old_thread != new_thread) {
#ifdef __x86_64__
        arch::amd64::tss_set_rsp0(new_kstack);
#endif
        if (new_thread->pml4_phys) {
            kernel::memory::vmm::switch_to(new_thread->pml4_phys);
        }
        uintptr_t* old_sp = old_thread ? &old_thread->stack_pointer : nullptr;
        switch_context(old_sp, new_thread->stack_pointer);
    }

    kernel::irq_restore(flags);
}

void scheduler::block(thread_state reason) noexcept {
    auto* cur = cpu::this_cpu()->current_thread;
    if (!cur) return;
    cur->state = reason;
    schedule();
}

void scheduler::yield() noexcept {
    schedule();
}

void scheduler::unblock(thread* t) noexcept {
    t->state = thread_state::READY;
    scheduler::add_thread(t);
}

thread* scheduler::get_thread_by_tid(uint32_t tid) noexcept {
    kernel::irq_lock_guard guard(g_threads_lock);
    thread* cur = g_all_threads;
    while (cur) {
        if (cur->tid == tid) return cur;
        cur = cur->all_next;
    }
    return nullptr;
}

thread* scheduler::current_thread() noexcept {
    return cpu::this_cpu()->current_thread;
}

void scheduler::wait_for_irq(uint8_t irq) noexcept {
    auto* cur = cpu::this_cpu()->current_thread;
    if (!cur) return;
    g_irq_waiters[irq] = cur;
    block(thread_state::BLOCKED);
}

void scheduler::wake_irq_waiters(uint8_t irq) noexcept {
    if (g_irq_waiters[irq]) {
        unblock(g_irq_waiters[irq]);
        g_irq_waiters[irq] = nullptr;
    }
}

} // namespace kernel::scheduler
