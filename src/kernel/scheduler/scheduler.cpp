// SPDX-License-Identifier: MIT
#if defined(__arm__)
#include <arch/armv7/exception.hpp>
#endif
#include <kernel/cpu/percpu.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/memory/user_access.hpp>
#include <kernel/memory/vma.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
#include <kernel/process/signal.hpp>
#include <kernel/scheduler/dispatch.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/sync/futex.hpp>
#include <kernel/sync/rcu.hpp>
#include <kernel/sync/spinlock.hpp>
#include <kernel/time.hpp>
#include <kernel/trace.hpp>
#include <kernel/vfs/tty.hpp>
#include <kernel/vfs/vfs.hpp>
#include <knew.hpp>
#include <uapi/kernel/top.h>

#ifdef __x86_64__
#include <arch/amd64/tss.hpp>
#endif

namespace kernel::scheduler {

// Global thread list — protected by g_threads_lock
static thread* g_all_threads = nullptr;
static kernel::irq_spinlock g_threads_lock;
// Kernel/idle threads must leave an outgoing user root inactive before reaping.
static kernel::atomic<uintptr_t> g_kernel_page_table{0};

static thread* g_irq_waiters[256] = {nullptr};
static kernel::irq_spinlock g_irq_waiters_lock;

// Global sleep queue — protected by g_sleep_queue_lock
static thread* g_sleep_queue_head = nullptr;
static kernel::irq_spinlock g_sleep_queue_lock;

extern "C" void switch_thread_context(uintptr_t* old_stack, uintptr_t new_stack) noexcept;
extern "C" {
void jump_to_user_space(void* entry, void* stack, void* arg);
#if defined(__aarch64__)
// Unmasks IRQs before calling the thread entry point held in x19.
void aarch64_thread_entry();
#endif
}

static thread* find_thread_locked(uint32_t tid, thread** prev_out = nullptr) noexcept {
    thread* prev = nullptr;
    thread* cur = g_all_threads;
    while (cur) {
        if (cur->tid == tid) {
            if (prev_out) *prev_out = prev;
            return cur;
        }
        prev = cur;
        cur = cur->all_next;
    }
    return nullptr;
}

static bool can_reap_locked(const thread* owner) noexcept {
    if (!dispatch::reclaimable(owner)) return false;
    if (!owner->pml4_phys) return true;
    for (auto* other = g_all_threads; other; other = other->all_next)
        if (other != owner && other->pml4_phys == owner->pml4_phys) return true;
    return !kernel::memory::vmm::is_active(owner->pml4_phys);
}

static void destroy_thread(thread* t) noexcept {
    if (!t) return;
    kernel::ipc::detach_thread(t);
    kernel::vfs::tty::detach_reader(t);
    {
        kernel::irq_lock_guard guard(g_irq_waiters_lock);
        for (auto& waiter : g_irq_waiters)
            if (waiter == t) waiter = nullptr;
    }
    {
        kernel::irq_lock_guard guard(g_sleep_queue_lock);
        auto** link = &g_sleep_queue_head;
        while (*link) {
            if (*link == t) {
                *link = t->next_sleeper;
                break;
            }
            link = &(*link)->next_sleeper;
        }
    }
    bool shared = false;
    for (auto* other = g_all_threads; other; other = other->all_next)
        if (other != t && other->pml4_phys == t->pml4_phys) shared = true;
    // Clone siblings share one mm. Tear down translations while VMA ownership
    // and borrowed-frame references remain available, then release those pins.
    if (t->pml4_phys && !shared) {
#ifdef RUCUX_VM_SELF_TEST
        const bool probe = t->mm && t->mm->smp_probe;
        const uint64_t seen = t->mm ? t->mm->cpus_seen : 0;
#endif
        kernel::memory::vmm::discard_address_space(t->pml4_phys, t->mm ? &t->mm->vmas : nullptr);
#ifdef RUCUX_VM_SELF_TEST
        if (probe) {
            const uint32_t count = cpu::g_cpu_count.load(kernel::acquire);
            const uint64_t expected = count == 64 ? UINT64_MAX : (uint64_t{1} << count) - 1;
            kernel::print("shared mm: root teardown {} (CPU mask {})\n", seen == expected ? "PASS" : "FAIL", seen);
        }
#endif
        kernel::memory::destroy_mm(t->mm);
        t->mm = nullptr;
    }
    delete[] reinterpret_cast<uint8_t*>(t->stack_base);
    kernel::vfs::vfs_manager::close_all(t);
    delete[] t->fd_table;
    delete t;
}

static void reap_thread_locked(thread* victim, thread* prev) noexcept {
    if (!victim) return;
    if (!can_reap_locked(victim)) {
        victim->parent_tid = 0; // Status consumed; defer until off CPU, off queue and unpinned.
        return;
    }
    if (prev)
        prev->all_next = victim->all_next;
    else
        g_all_threads = victim->all_next;
    destroy_thread(victim);
}

void scheduler::publish_termination(thread* child, int status) noexcept {
    if (!child) return;
    kernel::irq_lock_guard guard(g_threads_lock);
    child->exit_code = status;
    child->exited = true;
    child->state.store(thread_state::TERMINATED, kernel::release);
    // Local preemption cannot abandon the exiting stack between publication
    // and notification. A remote waitpid enrolls under this same lock.
    if (child->parent_tid == 0) return;
    for (auto* waiter = g_all_threads; waiter; waiter = waiter->all_next) {
        if (waiter->process_id == child->parent_tid && waiter->waiting_for_child &&
            (waiter->wait_target_tid == -1 || waiter->wait_target_tid == static_cast<int32_t>(child->tid))) {
            waiter->waiting_for_child = false;
            scheduler::unblock(waiter);
            break;
        }
    }
}

// ─── FD table ──────────────────────────────────────────────────────────────

bool thread::ensure_fd_capacity(size_t n) noexcept {
    if (n > MAX_FDS) n = MAX_FDS;
    if (fd_table && fd_count >= n) return true;

    size_t new_count = fd_count ? fd_count : INITIAL_FDS;
    while (new_count < n)
        new_count *= 2;
    if (new_count > MAX_FDS) new_count = MAX_FDS;

    auto* new_table = new file_descriptor[new_count];
    if (!new_table) return false;

    for (size_t i = 0; i < fd_count; ++i)
        new_table[i] = fd_table[i];
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

// ─── Public API ────────────────────────────────────────────────────────────

static void clone_trampoline() {
    thread* t = scheduler::scheduler::current_thread();
    kernel::print("userspace: entering EL0 at {}\n", t->user_entry);
    jump_to_user_space(t->user_entry, t->user_stack, t->user_arg);
}

static kernel::atomic<uint32_t> g_next_tid{1000};
static constexpr size_t THREAD_KERNEL_STACK_SIZE = 32 * 1024;

void idle_task() noexcept {
    while (true) {
#if defined(__x86_64__)
        asm volatile("sti; hlt");
#elif defined(__arm__)
        // Re-enable IRQs first; without this WFI the CPU will sleep forever.
        asm volatile("cpsie i; wfi");
#elif defined(__aarch64__)
        // daifclr #2 unmasks IRQ — same reasoning as ARMv7 above.
        asm volatile("msr daifclr, #2; wfi");
#endif
    }
}

void scheduler::init() noexcept {
    kernel::print("Scheduler initialized\n");

    auto* pcpu = cpu::this_cpu();
    uintptr_t unset = 0;
    g_kernel_page_table.compare_exchange_strong(unset, kernel::memory::vmm::get_active_page_table(), kernel::relaxed,
                                                kernel::relaxed);

    // Initialize per-CPU run queues
    for (int i = 0; i < static_cast<int>(thread_prio::NUM_PRIOS); ++i)
        pcpu->queues[i].init();
    pcpu->total_runnable = 0;

    // Create per-CPU idle thread
    auto* idle = new thread();
    idle->tid = 0;
    idle->priority = thread_prio::IDLE;
    idle->last_cpu = pcpu->cpu_id;
    idle->stack_size = 4096;
    idle->stack_base = reinterpret_cast<uintptr_t>(new uint8_t[idle->stack_size]);
    idle->pml4_phys = 0;
    idle->mm = nullptr;
    idle->creds = process::root_credentials(0, 0);

#if defined(__x86_64__)
    uint64_t* stack = reinterpret_cast<uint64_t*>(idle->stack_base + idle->stack_size);
    *(--stack) = 0; // Synthetic return slot gives C++ entry RSP % 16 == 8.
    *(--stack) = reinterpret_cast<uint64_t>(idle_task);
    *(--stack) = 0;
    *(--stack) = 0;
    *(--stack) = 0;
    *(--stack) = 0;
    *(--stack) = 0;
    *(--stack) = 0;
    idle->stack_pointer = reinterpret_cast<uintptr_t>(stack);
#elif defined(__arm__)
    uint32_t* stack = reinterpret_cast<uint32_t*>(idle->stack_base + idle->stack_size);
    *(--stack) = reinterpret_cast<uint32_t>(idle_task);
    for (int i = 0; i < 8; ++i)
        *(--stack) = 0;
    idle->stack_pointer = reinterpret_cast<uintptr_t>(stack);
#elif defined(__aarch64__)
    // 12 callee-saved slots matching switch.S; slot 11 is x30, which `ret`
    // jumps to. SP must stay 16-byte aligned.
    uint64_t* stack = reinterpret_cast<uint64_t*>((idle->stack_base + idle->stack_size) & ~0xFULL);
    stack -= 12;
    for (int i = 0; i < 12; ++i)
        stack[i] = 0;
    stack[11] = reinterpret_cast<uint64_t>(idle_task);
    idle->stack_pointer = reinterpret_cast<uintptr_t>(stack);
#endif
    idle->state.store(thread_state::READY, kernel::release);
    idle->fd_table = nullptr;
    idle->fd_count = 0;
    idle->parent_tid = 0;
    idle->wait_target_tid = -1;
    idle->waiting_for_child = false;

    pcpu->idle_thread = idle;
    pcpu->current_thread = nullptr;
}

thread* scheduler::spawn(void (*entry)(), uint32_t tid) noexcept {
    thread* t = new thread();
    t->tid = tid;
    t->priority = thread_prio::NORMAL;
    t->last_cpu = cpu::this_cpu()->cpu_id;
    t->stack_size = THREAD_KERNEL_STACK_SIZE;
    t->stack_base = reinterpret_cast<uintptr_t>(new uint8_t[t->stack_size]);
    t->pml4_phys = 0;
    t->mm = nullptr;
    t->creds = process::root_credentials(tid, tid);

    // The initial frame must match what the architecture's switch_context pops.
#if defined(__x86_64__)
    uint64_t* stack = reinterpret_cast<uint64_t*>(t->stack_base + t->stack_size);
    *(--stack) = 0;
    *(--stack) = reinterpret_cast<uint64_t>(entry);
    *(--stack) = 0;
    *(--stack) = 0;
    *(--stack) = 0;
    *(--stack) = 0;
    *(--stack) = 0;
    *(--stack) = 0;
    t->stack_pointer = reinterpret_cast<uintptr_t>(stack);
#elif defined(__arm__)
    uint32_t* stack = reinterpret_cast<uint32_t*>(t->stack_base + t->stack_size);
    *(--stack) = reinterpret_cast<uint32_t>(entry);
    for (int i = 0; i < 8; ++i)
        *(--stack) = 0;
    t->stack_pointer = reinterpret_cast<uintptr_t>(stack);
#elif defined(__aarch64__)
    // A fresh thread is reached by `ret`, so it inherits PSTATE from the
    // switching context — usually the IRQ handler, where IRQs are masked.
    // Enter through a trampoline that unmasks them: entry in x19, trampoline
    // in x30.
    uint64_t* stack = reinterpret_cast<uint64_t*>((t->stack_base + t->stack_size) & ~0xFULL);
    stack -= 12;
    for (int i = 0; i < 12; ++i)
        stack[i] = 0;
    stack[0] = reinterpret_cast<uint64_t>(entry);                  // x19
    stack[11] = reinterpret_cast<uint64_t>(&aarch64_thread_entry); // x30
    t->stack_pointer = reinterpret_cast<uintptr_t>(stack);
#endif
    t->async_head = 0;
    t->async_tail = 0;
    t->send_queue_head = nullptr;
    t->send_queue_next = nullptr;
    t->has_queued_msg = false;
    t->recv_buffer = nullptr;
    t->ipc_caller = nullptr;
    t->ipc_waiting = false;
    for (int s = 0; s < thread::MAX_SIGNALS; ++s)
        t->sig_handlers[s] = nullptr;
    t->sig_mask.store(0, kernel::relaxed);
    t->sig_pending.store(0, kernel::relaxed);
    t->exit_code = 0;
    t->exited = false;
    t->process_id = t->tid;
    t->parent_tid = 0;
    t->wait_target_tid = -1;
    t->waiting_for_child = false;
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

thread* scheduler::spawn_user(uintptr_t pml4_phys, void* entry, void* stack, void* arg, uint32_t tid,
                              bool enqueue) noexcept {
    thread* t = new thread();
    if (!t) return nullptr;
    thread* parent = current_thread();

    if (tid == 0) {
        tid = g_next_tid.fetch_add(1, kernel::relaxed) + 1;
    }

    t->tid = tid;
    t->priority = thread_prio::NORMAL;
    t->last_cpu = cpu::this_cpu()->cpu_id;
    t->stack_size = THREAD_KERNEL_STACK_SIZE;
    t->stack_base = reinterpret_cast<uintptr_t>(new uint8_t[t->stack_size]);
    t->pml4_phys = pml4_phys;
    t->mm = nullptr;
    t->creds = parent ? parent->creds : process::root_credentials(tid, tid);
    if (t->creds.sid == 0) t->creds.sid = tid;
    if (t->creds.pgid == 0) t->creds.pgid = tid;

    t->fd_table = nullptr;
    t->fd_count = 0;
    t->async_head = 0;
    t->async_tail = 0;
    t->send_queue_head = nullptr;
    t->send_queue_next = nullptr;
    t->has_queued_msg = false;
    t->recv_buffer = nullptr;
    t->user_entry = entry;
    t->user_stack = stack;
    t->user_arg = arg;
    t->user_saved_sp = 0;
    t->user_saved_lr = 0;
    t->user_saved_spsr = 0;
    t->futex_wait_addr = 0;
    t->ipc_caller = nullptr;
    t->ipc_waiting = false;
    for (int s = 0; s < thread::MAX_SIGNALS; ++s)
        t->sig_handlers[s] = nullptr;
    t->sig_mask.store(0, kernel::relaxed);
    t->sig_pending.store(0, kernel::relaxed);
    t->exit_code = 0;
    t->exited = false;
    t->process_id = t->tid;
    t->parent_tid = parent ? parent->process_id : 0;
    t->wait_target_tid = -1;
    t->waiting_for_child = false;
    t->wake_tick = 0;
    t->next_sleeper = nullptr;
    t->wait_next = nullptr;

#if defined(__x86_64__)
    uint64_t* kstack = reinterpret_cast<uint64_t*>(t->stack_base + t->stack_size);
    *(--kstack) = 0;
    *(--kstack) = reinterpret_cast<uint64_t>(clone_trampoline);
    *(--kstack) = 0;
    *(--kstack) = 0;
    *(--kstack) = 0;
    *(--kstack) = 0;
    *(--kstack) = 0;
    *(--kstack) = 0;
    t->stack_pointer = reinterpret_cast<uintptr_t>(kstack);
#elif defined(__arm__)
    uint32_t* kstack = reinterpret_cast<uint32_t*>(t->stack_base + t->stack_size);
    *(--kstack) = reinterpret_cast<uint32_t>(clone_trampoline);
    for (int i = 0; i < 8; ++i)
        *(--kstack) = 0;
    t->stack_pointer = reinterpret_cast<uintptr_t>(kstack);
#elif defined(__aarch64__)
    uint64_t* kstack = reinterpret_cast<uint64_t*>((t->stack_base + t->stack_size) & ~0xFULL);
    kstack -= 12;
    for (int i = 0; i < 12; ++i)
        kstack[i] = 0;
    kstack[11] = reinterpret_cast<uint64_t>(clone_trampoline);
    t->stack_pointer = reinterpret_cast<uintptr_t>(kstack);
#endif

    if (pml4_phys != 0) {
        t->mm = kernel::memory::create_mm();
        if (!t->mm) {
            delete[] reinterpret_cast<uint8_t*>(t->stack_base);
            delete t;
            return nullptr;
        }
    }

    {
        kernel::irq_lock_guard guard(g_threads_lock);
        t->all_next = g_all_threads;
        g_all_threads = t;
    }

    if (enqueue) add_thread(t);
    return t;
}

long scheduler::sys_clone(void* entry, void* stack, void* arg) noexcept {
    thread* parent = current_thread();
    if (!parent) return -1;
#if defined(__x86_64__)
    // A cloned entry is a C function, unlike an ELF _start entry point.
    uintptr_t return_slot = (reinterpret_cast<uintptr_t>(stack) & ~15ULL) - 8;
    uintptr_t sentinel = 0;
    if (!kernel::memory::copy_to_user(reinterpret_cast<void*>(return_slot), &sentinel, sizeof(sentinel))) return -14;
    stack = reinterpret_cast<void*>(return_slot);
#endif
    uint32_t owner_tid = parent->process_id;

    thread* t = new thread();
    if (!t) return -12;
    t->tid = g_next_tid.fetch_add(1, kernel::relaxed) + 1;
    t->priority = parent->priority;
    t->last_cpu = cpu::this_cpu()->cpu_id;
    t->stack_size = THREAD_KERNEL_STACK_SIZE;
    t->stack_base = reinterpret_cast<uintptr_t>(new uint8_t[t->stack_size]);
    if (!t->stack_base) {
        delete t;
        return -12;
    }
    t->pml4_phys = parent->pml4_phys;
    t->mm = parent->mm;
    t->creds = parent->creds;

    t->fd_table = nullptr;
    t->fd_count = 0;
    const int copied = kernel::vfs::vfs_manager::copy_fds(parent, t);
    if (copied < 0) {
        delete[] reinterpret_cast<uint8_t*>(t->stack_base);
        delete[] t->fd_table;
        delete t;
        return copied;
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
    t->user_saved_sp = 0;
    t->user_saved_lr = 0;
    t->user_saved_spsr = 0;
    t->futex_wait_addr = 0;
    t->ipc_caller = nullptr;
    t->ipc_waiting = false;
    t->process_id = owner_tid;
    t->parent_tid = owner_tid;
    t->wait_target_tid = -1;
    t->waiting_for_child = false;
    t->wake_tick = 0;
    t->next_sleeper = nullptr;
    t->wait_next = nullptr;

#if defined(__x86_64__)
    uint64_t* kstack = reinterpret_cast<uint64_t*>(t->stack_base + t->stack_size);
    *(--kstack) = 0;
    *(--kstack) = reinterpret_cast<uint64_t>(clone_trampoline);
    *(--kstack) = 0;
    *(--kstack) = 0;
    *(--kstack) = 0;
    *(--kstack) = 0;
    *(--kstack) = 0;
    *(--kstack) = 0;
    t->stack_pointer = reinterpret_cast<uintptr_t>(kstack);
#elif defined(__arm__)
    uint32_t* kstack = reinterpret_cast<uint32_t*>(t->stack_base + t->stack_size);
    *(--kstack) = reinterpret_cast<uint32_t>(clone_trampoline);
    for (int i = 0; i < 8; ++i)
        *(--kstack) = 0;
    t->stack_pointer = reinterpret_cast<uintptr_t>(kstack);
#elif defined(__aarch64__)
    uint64_t* kstack = reinterpret_cast<uint64_t*>((t->stack_base + t->stack_size) & ~0xFULL);
    kstack -= 12;
    for (int i = 0; i < 12; ++i)
        kstack[i] = 0;
    kstack[11] = reinterpret_cast<uint64_t>(clone_trampoline);
    t->stack_pointer = reinterpret_cast<uintptr_t>(kstack);
#endif

    {
        kernel::irq_lock_guard guard(g_threads_lock);
        t->all_next = g_all_threads;
        g_all_threads = t;
    }

    add_thread(t);
    return t->tid;
}

// ─── Enqueue ───────────────────────────────────────────────────────────────

void scheduler::add_thread(thread* t) noexcept {
    dispatch::enqueue(t);
}

// Runs before the new context returns/enables IRQs, including fresh threads.
extern "C" void scheduler_finish_switch() noexcept {
    dispatch::finish_switch(cpu::this_cpu());
}

// ─── Schedule (per-CPU) ────────────────────────────────────────────────────

void scheduler::cleanup_terminated() noexcept {
    auto* this_thread = cpu::this_cpu()->current_thread;

    kernel::irq_lock_guard guard(g_threads_lock);
    thread* prev = nullptr;
    thread* cur = g_all_threads;
    while (cur) {
        if (cur->state.load(kernel::acquire) == thread_state::TERMINATED && cur != this_thread) {
            // Defer reaping if the parent is still alive; sys_waitpid()
            // will collect the corpse and consume the exit status.
            if (!can_reap_locked(cur) || (cur->process_id == cur->tid && cur->parent_tid != 0 &&
                                          find_thread_locked(cur->parent_tid) != nullptr)) {
                prev = cur;
                cur = cur->all_next;
                continue;
            }
            thread* to_delete = cur;
            if (prev)
                prev->all_next = cur->all_next;
            else
                g_all_threads = cur->all_next;
            cur = cur->all_next;
            destroy_thread(to_delete);
        } else {
            prev = cur;
            cur = cur->all_next;
        }
    }
}

void scheduler::schedule() noexcept {
    uintptr_t flags = kernel::irq_save();
    auto* pcpu = cpu::this_cpu();

    // Idle/same-thread passes must also advance RCU grace periods.
    kernel::rcu::note_quiescent_state();

    cleanup_terminated();

    thread* old_thread = pcpu->current_thread;
    thread* new_thread = nullptr;

    for (;;) {
        new_thread = dispatch::choose(pcpu);
        if (new_thread == pcpu->idle_thread || !kernel::process::signal_manager::consume_fatal_signal(new_thread))
            break;
        // Selection reserves the candidate through signal/descriptor cleanup
        // and parent notification. Do not expose an unclaimed raw pointer.
        if (new_thread != old_thread) dispatch::release_candidate(new_thread);
        cleanup_terminated();
    }
    dispatch::prepare_switch(pcpu, new_thread);

    uintptr_t new_kstack = new_thread->stack_base + new_thread->stack_size;
    pcpu->kernel_stack = new_kstack;

    if (old_thread != new_thread) {
        uint64_t old_tid = old_thread ? old_thread->tid : 0;
        TRACE_INSTANT(TRACE_EVENT_SCHED_SWITCH, old_tid, new_thread->tid);
#ifdef __x86_64__
        arch::amd64::tss_set_rsp0(new_kstack);
#endif
#if defined(__arm__)
        if (old_thread && old_thread->pml4_phys) {
            arch::armv7::save_user_return_context(&old_thread->user_saved_sp, &old_thread->user_saved_lr,
                                                  &old_thread->user_saved_spsr);
        }
        if (new_thread->pml4_phys && new_thread->user_saved_spsr != 0) {
            arch::armv7::restore_user_return_context(new_thread->user_saved_sp, new_thread->user_saved_lr,
                                                     new_thread->user_saved_spsr);
        }
#endif
        const uintptr_t root =
            new_thread->pml4_phys ? new_thread->pml4_phys : g_kernel_page_table.load(kernel::relaxed);
        kernel::memory::vmm::switch_to(root, new_thread->mm);
        uintptr_t* old_sp = old_thread ? &old_thread->stack_pointer : nullptr;
#if defined(__x86_64__)
        arch::amd64::switch_fpu(old_thread ? &old_thread->fpu : nullptr, new_thread->fpu);
#endif
        switch_thread_context(old_sp, new_thread->stack_pointer);
    }

    // Process any pending RCU callbacks
    kernel::rcu::process_callbacks();

    kernel::irq_restore(flags);
}

// ─── Block / Unblock / Yield ───────────────────────────────────────────────

void scheduler::block(thread_state reason) noexcept {
    auto* cur = cpu::this_cpu()->current_thread;
    if (!cur) return;
    TRACE_INSTANT(TRACE_EVENT_THREAD_BLOCK, cur->tid, static_cast<uint64_t>(reason));
    cur->state.store(reason, kernel::release);
    schedule();
}

void scheduler::yield() noexcept {
    schedule();
}

void scheduler::unblock(thread* t) noexcept {
    if (!t) return;
    kernel::irq_lock_guard guard(t->wait_lock);
    // Cancellation and a normal event may race. An interruptible enrollment
    // must be queued once, even if the event arrives before schedule().
    if (t->state.load(kernel::acquire) == thread_state::TERMINATED) return;
    if (t->interruptible_wait && t->state.load(kernel::acquire) != thread_state::BLOCKED) return;
    add_thread(t);
}

void scheduler::sleep_until(uint64_t tick) noexcept {
    auto* cur = cpu::this_cpu()->current_thread;
    if (!cur) return;
    const uintptr_t flags = kernel::irq_save();
    bool enrolled = false;
    if (tick > kernel::time_manager::get_ticks()) {
        kernel::irq_lock_guard guard(g_sleep_queue_lock);
        if (cur->begin_interruptible_wait()) {
            cur->wake_tick = tick;
            thread** ptr = &g_sleep_queue_head;
            while (*ptr && (*ptr)->wake_tick <= tick)
                ptr = &(*ptr)->next_sleeper;
            cur->next_sleeper = *ptr;
            *ptr = cur;
            enrolled = true;
        }
    }
    if (enrolled) {
        schedule();
        kernel::irq_lock_guard guard(g_sleep_queue_lock);
        auto** link = &g_sleep_queue_head;
        while (*link && *link != cur)
            link = &(*link)->next_sleeper;
        if (*link) *link = cur->next_sleeper;
        cur->next_sleeper = nullptr;
        cur->wake_tick = 0;
        cur->end_interruptible_wait();
    }
    kernel::irq_restore(flags);
}

void scheduler::check_sleepers(uint64_t current_tick) noexcept {
    kernel::irq_lock_guard guard(g_sleep_queue_lock);
    while (g_sleep_queue_head && g_sleep_queue_head->wake_tick <= current_tick) {
        auto* sleeper = g_sleep_queue_head;
        g_sleep_queue_head = sleeper->next_sleeper;
        sleeper->next_sleeper = nullptr;
        sleeper->wake_tick = 0;
        // The resuming thread must reacquire this lock before its wait scope
        // ends. Do not carry an unpinned thread pointer into a post-lock list.
        unblock(sleeper);
    }
}

void scheduler::exit(int status) noexcept {
    auto* cur = cpu::this_cpu()->current_thread;
    if (!cur) return;
    TRACE_INSTANT(TRACE_EVENT_THREAD_EXIT, cur->tid, 0);
    // Close callbacks may take subsystem locks; run before marking the thread
    // reapable and without the global thread-list lock.
    kernel::ipc::detach_thread(cur);
    kernel::vfs::vfs_manager::close_all(cur);
    publish_termination(cur, (status & 0xff) << 8);
    finish_termination();
}

void scheduler::finish_termination() noexcept {
    auto* cur = current_thread();
    if (!cur) return;
    schedule();
    while (true) {
#if defined(__x86_64__)
        asm volatile("hlt");
#elif defined(__arm__)
        asm volatile("wfi");
#elif defined(__aarch64__)
        asm volatile("wfi");
#endif
    }
}

// ─── Thread lookup ─────────────────────────────────────────────────────────

int scheduler::signal_thread(int pid, int sig) noexcept {
    auto* sender = current_thread();
    if (!sender) return -1;
    kernel::irq_lock_guard guard(g_threads_lock);
    auto* target =
        pid == 0 || static_cast<uint32_t>(pid) == sender->tid ? sender : find_thread_locked(static_cast<uint32_t>(pid));
    if (!target || target->state.load(kernel::acquire) == thread_state::TERMINATED) return -3;
    if (!kernel::process::may_signal(sender->creds, sender->tid, target->creds, target->tid)) return -1;
    return kernel::process::signal_manager::post_signal(target, sig);
}

thread* scheduler::pin_thread(uint32_t tid) noexcept {
    kernel::irq_lock_guard guard(g_threads_lock);
    auto* target = find_thread_locked(tid);
    if (!target || target->state.load(kernel::acquire) == thread_state::TERMINATED ||
        target->ipc_closed.load(kernel::acquire))
        return nullptr;
    target->pin();
    return target;
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

int scheduler::sys_top(void* buffer, size_t size) noexcept {
    if (!buffer || size < 8) return -1; // Need at least size for num_processes

    uint32_t* num_processes = static_cast<uint32_t*>(buffer);
    *num_processes = 0;

    top_process_info* processes = reinterpret_cast<top_process_info*>(num_processes + 1);

    size_t capacity = (size - sizeof(uint32_t)) / sizeof(top_process_info);
    if (capacity == 0) return 0;

    kernel::irq_lock_guard guard(g_threads_lock);
    thread* cur = g_all_threads;
    while (cur && *num_processes < capacity) {
        processes[*num_processes].tid = cur->tid;
        processes[*num_processes].state = static_cast<uint32_t>(cur->state.load(kernel::acquire));
        processes[*num_processes].priority = static_cast<uint8_t>(cur->priority);
        processes[*num_processes].cpu = dispatch::cpu_of(cur);
        (*num_processes)++;
        cur = cur->all_next;
    }

    return 0;
}

long scheduler::sys_waitpid(int pid, int* wstatus, int options) noexcept {
    constexpr int WNOHANG = 1;
    if ((options & ~WNOHANG) || pid == 0 || pid < -1) return -22;
    auto* current = current_thread();
    if (!current) return -1;
    const uint32_t owner_tid = current->process_id;
    while (true) {
        {
            kernel::irq_lock_guard guard(g_threads_lock);
            current->waiting_for_child = false;
            current->wait_target_tid = -1;
            if (current->fatal_signal_pending()) return -4;
            bool has_child = false;
            thread* previous = nullptr;
            for (auto* child = g_all_threads; child; child = child->all_next) {
                const bool matches = child->parent_tid == owner_tid && child->process_id == child->tid &&
                                     child != current && (pid == -1 || static_cast<uint32_t>(pid) == child->tid);
                if (matches) {
                    has_child = true;
                    if (child->state.load(kernel::acquire) == thread_state::TERMINATED) {
                        const int status = child->exit_code;
                        const auto tid = child->tid;
                        reap_thread_locked(child, previous);
                        if (wstatus) *wstatus = status;
                        return tid;
                    }
                }
                previous = child;
            }
            if (!has_child) return -10;
            if (options & WNOHANG) return 0;
            if (!current->begin_interruptible_wait()) return -4;
            current->wait_target_tid = pid;
            current->waiting_for_child = true;
        }
        schedule();
        {
            kernel::irq_lock_guard guard(g_threads_lock);
            current->waiting_for_child = false;
            current->wait_target_tid = -1;
            current->end_interruptible_wait();
        }
    }
}

// ─── IRQ waiters ───────────────────────────────────────────────────────────

void scheduler::wait_for_irq(uint8_t irq) noexcept {
    auto* cur = current_thread();
    if (!cur) return;
    {
        kernel::irq_lock_guard guard(g_irq_waiters_lock);
        if (!cur->begin_interruptible_wait()) return;
        g_irq_waiters[irq] = cur;
    }
    schedule();
    {
        kernel::irq_lock_guard guard(g_irq_waiters_lock);
        if (g_irq_waiters[irq] == cur) g_irq_waiters[irq] = nullptr;
        cur->end_interruptible_wait();
    }
}

void scheduler::wake_irq_waiters(uint8_t irq) noexcept {
    kernel::irq_lock_guard guard(g_irq_waiters_lock);
    auto* waiter = g_irq_waiters[irq];
    g_irq_waiters[irq] = nullptr;
    if (waiter) {
        TRACE_INSTANT(TRACE_EVENT_IRQ_WAKE, irq, waiter->tid);
        unblock(waiter);
    }
}

// ─── Futex (delegates to scalable hash table) ─────────────────────────────

long scheduler::sys_futex(uint32_t* uaddr, int op, uint32_t val) noexcept {
    if (op == 0) return kernel::sync::futex_wait(uaddr, val);
    if (op == 1) return kernel::sync::futex_wake(uaddr, val);
    return -1;
}

} // namespace kernel::scheduler
