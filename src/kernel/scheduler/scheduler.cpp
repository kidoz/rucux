// SPDX-License-Identifier: MIT
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <new>

#ifdef __x86_64__
#include <arch/amd64/tss.hpp>
#endif

namespace kernel::scheduler {

static thread* g_current_thread = nullptr;
static thread* g_ready_queue = nullptr;
static thread* g_ready_tail = nullptr;
static thread* g_all_threads = nullptr;
static thread* g_irq_waiters[256] = {nullptr};

extern "C" void switch_context(uintptr_t* old_stack, uintptr_t new_stack) noexcept;
extern "C" {
uintptr_t g_current_kernel_stack = 0;
uintptr_t g_temp_user_rsp = 0;
void jump_to_user_space(void* entry, void* stack, void* arg);
}

static void clone_trampoline() {
    thread* t = scheduler::scheduler::current_thread();
    jump_to_user_space(t->user_entry, t->user_stack, t->user_arg);
}

static uint32_t g_next_tid = 1000;

long scheduler::sys_clone(void* entry, void* stack, void* arg) noexcept {
    thread* parent = current_thread();
    if (!parent) return -1;

    thread* t = new thread();
    t->tid = ++g_next_tid;
    t->stack_size = 8192;
    t->stack_base = reinterpret_cast<uintptr_t>(new uint8_t[t->stack_size]);
    t->pml4_phys = parent->pml4_phys; // Share address space

    // Copy FDs
    for (size_t i = 0; i < thread::MAX_FDS; ++i) {
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

    t->all_next = g_all_threads;
    g_all_threads = t;

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

static thread* g_idle_thread = nullptr;

void idle_task() {
    while (true) {
        asm volatile("sti; hlt");
    }
}

void scheduler::init() noexcept {
    kernel::print("Scheduler initialized\n");
    // Spawn the idle thread, it runs in kernel space
    g_idle_thread = new thread();
    g_idle_thread->tid = 0;
    g_idle_thread->stack_size = 4096;
    g_idle_thread->stack_base = reinterpret_cast<uintptr_t>(new uint8_t[g_idle_thread->stack_size]);
    g_idle_thread->pml4_phys = 0; // Kernel CR3

    uint64_t* stack = reinterpret_cast<uint64_t*>(g_idle_thread->stack_base + g_idle_thread->stack_size);
    *(--stack) = reinterpret_cast<uint64_t>(idle_task);
    *(--stack) = 0; // rbp
    *(--stack) = 0; // rbx
    *(--stack) = 0; // r12
    *(--stack) = 0; // r13
    *(--stack) = 0; // r14
    *(--stack) = 0; // r15
    g_idle_thread->stack_pointer = reinterpret_cast<uintptr_t>(stack);
    g_idle_thread->state = thread_state::READY;
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

    for (size_t i = 0; i < thread::MAX_FDS; ++i) {
        t->fd_table[i].node = nullptr;
        t->fd_table[i].offset = 0;
        t->fd_table[i].flags = 0;
    }

    t->all_next = g_all_threads;
    g_all_threads = t;

    add_thread(t);
    return t;
}

void scheduler::add_thread(thread* t) noexcept {
    t->state = thread_state::READY;
    t->next = nullptr;

    if (!g_ready_queue) {
        g_ready_queue = t;
        g_ready_tail = t;
    } else {
        g_ready_tail->next = t;
        g_ready_tail = t;
    }
}

void scheduler::cleanup_terminated() noexcept {
    thread* prev = nullptr;
    thread* cur = g_all_threads;
    while (cur) {
        if (cur->state == thread_state::TERMINATED && cur != g_current_thread) {
            thread* to_delete = cur;
            if (prev) {
                prev->all_next = cur->all_next;
            } else {
                g_all_threads = cur->all_next;
            }
            cur = cur->all_next;

            // Free stack
            delete[] reinterpret_cast<uint8_t*>(to_delete->stack_base);
            delete to_delete;
        } else {
            prev = cur;
            cur = cur->all_next;
        }
    }
}

void scheduler::exit() noexcept {
    if (!g_current_thread) return;
    g_current_thread->state = thread_state::TERMINATED;
    schedule();
    while (true) {
        asm volatile("hlt");
    }
}

void scheduler::schedule() noexcept {
    cleanup_terminated();

    thread* old_thread = g_current_thread;
    thread* new_thread = nullptr;

    if (!g_ready_queue) {
        if (g_current_thread &&
            (g_current_thread->state == thread_state::RUNNING || g_current_thread->state == thread_state::READY)) {
            return; // Safe to resume current thread
        }
        // Switch to idle thread
        new_thread = g_idle_thread;
    } else {
        // Simple round-robin: move new_thread to tail and run it
        new_thread = g_ready_queue;
        g_ready_queue = new_thread->next;
        new_thread->next = nullptr;

        if (g_ready_tail == new_thread) {
            // Only one thread was ready
            g_ready_tail = g_ready_queue;
        } else {
            if (!g_ready_queue) {
                g_ready_tail = nullptr;
            }
        }
    }

    if (old_thread && old_thread->state == thread_state::RUNNING && old_thread != g_idle_thread) {
        old_thread->state = thread_state::READY;
        scheduler::add_thread(old_thread);
    }

    new_thread->state = thread_state::RUNNING;
    g_current_thread = new_thread;
    g_current_kernel_stack = new_thread->stack_base + new_thread->stack_size;

    if (old_thread != new_thread) {
#ifdef __x86_64__
        arch::amd64::tss_set_rsp0(new_thread->stack_base + new_thread->stack_size);
#endif
        if (new_thread->pml4_phys) {
            kernel::memory::vmm::switch_to(new_thread->pml4_phys);
        }
        uintptr_t* old_sp = old_thread ? &old_thread->stack_pointer : nullptr;
        switch_context(old_sp, new_thread->stack_pointer);
    }
}

void scheduler::block(thread_state reason) noexcept {
    if (!g_current_thread) return;
    g_current_thread->state = reason;
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
    thread* cur = g_all_threads;
    while (cur) {
        if (cur->tid == tid) return cur;
        cur = cur->all_next;
    }
    return nullptr;
}

thread* scheduler::current_thread() noexcept {
    return g_current_thread;
}

void scheduler::wait_for_irq(uint8_t irq) noexcept {
    if (!g_current_thread) return;
    g_irq_waiters[irq] = g_current_thread;
    block(thread_state::BLOCKED);
}

void scheduler::wake_irq_waiters(uint8_t irq) noexcept {
    if (g_irq_waiters[irq]) {
        unblock(g_irq_waiters[irq]);
        g_irq_waiters[irq] = nullptr;
    }
}

} // namespace kernel::scheduler
