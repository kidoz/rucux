// SPDX-License-Identifier: MIT
#include <kernel/cpu/percpu.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/sync/futex.hpp>
#include <kernel/sync/rcu.hpp>
#include <kernel/sync/spinlock.hpp>
#include <knew.hpp>

#ifdef __x86_64__
#include <arch/amd64/tss.hpp>
#endif

namespace kernel::scheduler {

// Global thread list — protected by g_threads_lock
static thread* g_all_threads = nullptr;
static kernel::irq_spinlock g_threads_lock;

static thread* g_irq_waiters[256] = {nullptr};

extern "C" void switch_context(uintptr_t* old_stack, uintptr_t new_stack) noexcept;
extern "C" {
uintptr_t g_current_kernel_stack = 0;
uintptr_t g_temp_user_rsp = 0;
void jump_to_user_space(void* entry, void* stack, void* arg);
}

static void destroy_thread(thread* t) noexcept {
    if (!t) return;
    delete[] reinterpret_cast<uint8_t*>(t->stack_base);
    delete[] t->fd_table;
    delete t;
}

// ─── FD table ──────────────────────────────────────────────────────────────

bool thread::ensure_fd_capacity(size_t n) noexcept {
    if (n > MAX_FDS) n = MAX_FDS;
    if (fd_table && fd_count >= n) return true;

    size_t new_count = fd_count ? fd_count : INITIAL_FDS;
    while (new_count < n) new_count *= 2;
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

// ─── Per-CPU run queue helpers ─────────────────────────────────────────────

// Enqueue a thread on a specific CPU's run queue (caller must hold cpu's sched_lock)
static void enqueue_on_cpu(cpu::per_cpu* pcpu, thread* t) noexcept {
    auto prio_idx = static_cast<uint8_t>(t->priority);
    pcpu->queues[prio_idx].enqueue(t);
    pcpu->total_runnable++;
}

// Dequeue highest-priority thread from a CPU (caller must hold sched_lock)
static thread* dequeue_from_cpu(cpu::per_cpu* pcpu) noexcept {
    for (int p = 0; p < static_cast<int>(thread_prio::NUM_PRIOS); ++p) {
        thread* t = pcpu->queues[p].dequeue();
        if (t) {
            pcpu->total_runnable--;
            return t;
        }
    }
    return nullptr;
}

// Find the busiest online CPU (for work stealing)
static cpu::per_cpu* find_busiest_cpu(uint32_t exclude_id) noexcept {
    cpu::per_cpu* busiest = nullptr;
    uint32_t max_load = 0;
    uint32_t ncpus = cpu::g_cpu_count.load(kernel::relaxed);

    for (uint32_t i = 0; i < ncpus; ++i) {
        if (i == exclude_id) continue;
        auto* c = cpu::get_cpu(i);
        if (!c->online) continue;
        if (c->total_runnable > max_load) {
            max_load = c->total_runnable;
            busiest = c;
        }
    }
    return (max_load > 1) ? busiest : nullptr; // Only steal if > 1 thread
}

// Try to steal a thread from another CPU's run queue
static thread* try_steal(cpu::per_cpu* thief) noexcept {
    auto* victim = find_busiest_cpu(thief->cpu_id);
    if (!victim) return nullptr;

    uintptr_t flags = victim->sched_lock.lock();
    thread* stolen = nullptr;

    // Steal from lowest priority first (least impactful)
    for (int p = static_cast<int>(thread_prio::NUM_PRIOS) - 1; p >= 0; --p) {
        if (!victim->queues[p].empty()) {
            stolen = victim->queues[p].dequeue();
            victim->total_runnable--;
            break;
        }
    }

    victim->sched_lock.unlock(flags);
    return stolen;
}

// ─── Public API ────────────────────────────────────────────────────────────

static void clone_trampoline() {
    thread* t = scheduler::scheduler::current_thread();
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
#endif
    }
}

void scheduler::init() noexcept {
    kernel::print("Scheduler initialized\n");

    auto* pcpu = cpu::this_cpu();

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

#if defined(__x86_64__)
    uint64_t* stack = reinterpret_cast<uint64_t*>(idle->stack_base + idle->stack_size);
    *(--stack) = reinterpret_cast<uint64_t>(idle_task);
    *(--stack) = 0; *(--stack) = 0; *(--stack) = 0;
    *(--stack) = 0; *(--stack) = 0; *(--stack) = 0;
    idle->stack_pointer = reinterpret_cast<uintptr_t>(stack);
#elif defined(__arm__)
    uint32_t* stack = reinterpret_cast<uint32_t*>(idle->stack_base + idle->stack_size);
    *(--stack) = reinterpret_cast<uint32_t>(idle_task);
    for (int i = 0; i < 8; ++i) *(--stack) = 0;
    idle->stack_pointer = reinterpret_cast<uintptr_t>(stack);
#endif
    idle->state = thread_state::READY;
    idle->fd_table = nullptr;
    idle->fd_count = 0;

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

    uint64_t* stack = reinterpret_cast<uint64_t*>(t->stack_base + t->stack_size);
    *(--stack) = reinterpret_cast<uint64_t>(entry);
    *(--stack) = 0; *(--stack) = 0; *(--stack) = 0;
    *(--stack) = 0; *(--stack) = 0; *(--stack) = 0;

    t->stack_pointer = reinterpret_cast<uintptr_t>(stack);
    t->async_head = 0;
    t->async_tail = 0;
    t->send_queue_head = nullptr;
    t->send_queue_next = nullptr;
    t->has_queued_msg = false;
    t->recv_buffer = nullptr;
    t->ipc_caller = nullptr;
    t->ipc_waiting = false;
    for (int s = 0; s < thread::MAX_SIGNALS; ++s) t->sig_handlers[s] = nullptr;
    t->sig_mask = 0;
    t->sig_pending = 0;
    t->exit_code = 0;
    t->exited = false;
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

long scheduler::sys_clone(void* entry, void* stack, void* arg) noexcept {
    thread* parent = current_thread();
    if (!parent) return -1;

    thread* t = new thread();
    t->tid = g_next_tid.fetch_add(1, kernel::relaxed) + 1;
    t->priority = parent->priority;
    t->last_cpu = cpu::this_cpu()->cpu_id;
    t->stack_size = THREAD_KERNEL_STACK_SIZE;
    t->stack_base = reinterpret_cast<uintptr_t>(new uint8_t[t->stack_size]);
    t->pml4_phys = parent->pml4_phys;

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
    t->ipc_caller = nullptr;
    t->ipc_waiting = false;

    uint64_t* kstack = reinterpret_cast<uint64_t*>(t->stack_base + t->stack_size);
    *(--kstack) = reinterpret_cast<uint64_t>(clone_trampoline);
    *(--kstack) = 0; *(--kstack) = 0; *(--kstack) = 0;
    *(--kstack) = 0; *(--kstack) = 0; *(--kstack) = 0;
    t->stack_pointer = reinterpret_cast<uintptr_t>(kstack);

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
    t->state = thread_state::READY;

    // Prefer the CPU this thread last ran on (cache affinity)
    uint32_t target_cpu = t->last_cpu;
    uint32_t ncpus = cpu::g_cpu_count.load(kernel::relaxed);
    if (target_cpu >= ncpus) target_cpu = 0;

    auto* pcpu = cpu::get_cpu(target_cpu);
    uintptr_t flags = pcpu->sched_lock.lock();
    enqueue_on_cpu(pcpu, t);
    pcpu->sched_lock.unlock(flags);
}

// ─── Schedule (per-CPU) ────────────────────────────────────────────────────

void scheduler::cleanup_terminated() noexcept {
    auto* this_thread = cpu::this_cpu()->current_thread;

    kernel::irq_lock_guard guard(g_threads_lock);
    thread* prev = nullptr;
    thread* cur = g_all_threads;
    while (cur) {
        if (cur->state == thread_state::TERMINATED && cur != this_thread) {
            thread* to_delete = cur;
            if (prev) prev->all_next = cur->all_next;
            else      g_all_threads = cur->all_next;
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

    cleanup_terminated();

    thread* old_thread = pcpu->current_thread;
    thread* new_thread = nullptr;

    {
        uintptr_t rq_flags = pcpu->sched_lock.lock();

        // Re-enqueue the old thread if it was running
        if (old_thread && old_thread->state == thread_state::RUNNING &&
            old_thread != pcpu->idle_thread) {
            old_thread->state = thread_state::READY;
            old_thread->last_cpu = pcpu->cpu_id;
            enqueue_on_cpu(pcpu, old_thread);
        }

        // Pick highest-priority runnable thread
        new_thread = dequeue_from_cpu(pcpu);

        pcpu->sched_lock.unlock(rq_flags);
    }

    // If nothing on local queue, try work stealing
    if (!new_thread) {
        new_thread = try_steal(pcpu);
    }

    // If still nothing, check if current thread can continue
    if (!new_thread) {
        if (old_thread &&
            (old_thread->state == thread_state::RUNNING || old_thread->state == thread_state::READY)) {
            kernel::irq_restore(flags);
            return;
        }
        new_thread = pcpu->idle_thread;
    }

    new_thread->state = thread_state::RUNNING;
    new_thread->last_cpu = pcpu->cpu_id;
    pcpu->current_thread = new_thread;

    uintptr_t new_kstack = new_thread->stack_base + new_thread->stack_size;
    pcpu->kernel_stack = new_kstack;
    g_current_kernel_stack = new_kstack;

    if (old_thread != new_thread) {
        // Context switch = quiescent state for RCU
        kernel::rcu::note_quiescent_state();
#ifdef __x86_64__
        arch::amd64::tss_set_rsp0(new_kstack);
#endif
        if (new_thread->pml4_phys) {
            kernel::memory::vmm::switch_to(new_thread->pml4_phys);
        }
        uintptr_t* old_sp = old_thread ? &old_thread->stack_pointer : nullptr;
        switch_context(old_sp, new_thread->stack_pointer);
    }

    // Process any pending RCU callbacks
    kernel::rcu::process_callbacks();

    kernel::irq_restore(flags);
}

// ─── Block / Unblock / Yield ───────────────────────────────────────────────

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
    add_thread(t);
}

void scheduler::exit() noexcept {
    auto* cur = cpu::this_cpu()->current_thread;
    if (!cur) return;
    cur->exited = true;
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

// ─── Thread lookup ─────────────────────────────────────────────────────────

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

// ─── IRQ waiters ───────────────────────────────────────────────────────────

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

// ─── Futex (delegates to scalable hash table) ─────────────────────────────

long scheduler::sys_futex(uint32_t* uaddr, int op, uint32_t val) noexcept {
    if (op == 0) return kernel::sync::futex_wait(uaddr, val);
    if (op == 1) return kernel::sync::futex_wake(uaddr, val);
    return -1;
}

} // namespace kernel::scheduler
