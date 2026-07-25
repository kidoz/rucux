// SPDX-License-Identifier: MIT
#include <kernel/cpu/percpu.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/sync/spinlock.hpp>
#include <lib/string.hpp>

#ifdef __x86_64__
#include <arch/amd64/tss.hpp>
#endif

extern "C" void switch_context(uintptr_t* old_stack, uintptr_t new_stack) noexcept;
extern "C" uintptr_t g_current_kernel_stack;

namespace kernel::ipc {

// ─── Legacy IPC (unchanged) ───────────────────────────────────────────────

void ipc_manager::send_sync(uint32_t target_tid, const message& msg) noexcept {
    scheduler::thread* current = scheduler::scheduler::current_thread();
    scheduler::thread* target = scheduler::scheduler::get_thread_by_tid(target_tid);

    if (!target) {
        kernel::print("IPC: Target {} not found\n", target_tid);
        return;
    }

    current->queued_msg.sender = current->tid;
    current->queued_msg.type = msg.type;
    lib::memcpy(current->queued_msg.data, msg.data, sizeof(msg.data));
    current->has_queued_msg = true;
    current->send_queue_next = nullptr;

    if (!target->send_queue_head) {
        target->send_queue_head = current;
    } else {
        scheduler::thread* cur = target->send_queue_head;
        while (cur->send_queue_next)
            cur = cur->send_queue_next;
        cur->send_queue_next = current;
    }

    if (target->state == scheduler::thread_state::BLOCKED && target->recv_buffer) {
        scheduler::scheduler::unblock(target);
    }

    scheduler::scheduler::block(scheduler::thread_state::BLOCKED);
}

void ipc_manager::send_async(uint32_t target_tid, const message& msg) noexcept {
    scheduler::thread* target = scheduler::scheduler::get_thread_by_tid(target_tid);
    if (!target) return;

    size_t next_tail = (target->async_tail + 1) % scheduler::thread::ASYNC_QUEUE_SIZE;
    if (next_tail == target->async_head) return;

    auto& entry = target->async_queue[target->async_tail];
    entry.sender = scheduler::scheduler::current_thread()->tid;
    entry.type = msg.type;
    lib::memcpy(entry.data, msg.data, sizeof(msg.data));
    target->async_tail = next_tail;

    if (target->state == scheduler::thread_state::BLOCKED && target->recv_buffer == nullptr) {
        scheduler::scheduler::unblock(target);
    }
}

void ipc_manager::receive_sync(message& msg) noexcept {
    scheduler::thread* current = scheduler::scheduler::current_thread();

    if (current->async_head != current->async_tail) {
        auto& entry = current->async_queue[current->async_head];
        msg.sender = entry.sender;
        msg.type = entry.type;
        lib::memcpy(msg.data, entry.data, sizeof(msg.data));
        current->async_head = (current->async_head + 1) % scheduler::thread::ASYNC_QUEUE_SIZE;
        return;
    }

    if (current->send_queue_head) {
        scheduler::thread* sender = current->send_queue_head;
        current->send_queue_head = sender->send_queue_next;
        msg.sender = sender->queued_msg.sender;
        msg.type = sender->queued_msg.type;
        lib::memcpy(msg.data, sender->queued_msg.data, sizeof(msg.data));
        sender->has_queued_msg = false;
        scheduler::scheduler::unblock(sender);
        return;
    }

    current->recv_buffer = &msg;
    scheduler::scheduler::block(scheduler::thread_state::BLOCKED);

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

// ─── Fast IPC (register-based, direct thread switch) ──────────────────────
//
// The fast path performs a DIRECT context switch between sender and receiver,
// bypassing the scheduler's run queues entirely. This eliminates:
//   - Two run-queue enqueue/dequeue operations
//   - Two lock acquisitions on the scheduler
//   - The scheduling decision overhead
//
// Message data stays in the ipc_regs[] array (which maps 1:1 to CPU registers
// in the syscall ABI). A truly optimized implementation would keep data in
// actual CPU registers through the entire switch, but that requires custom
// assembly. This C++ implementation uses a 4-word buffer which the compiler
// keeps in registers or L1 cache.

// Direct thread switch: save current context, switch to target.
// Like switch_context but also handles CR3/TSS for cross-address-space calls.
static void direct_switch(scheduler::thread* from, scheduler::thread* to) noexcept {
    auto* pcpu = cpu::this_cpu();

    to->state = scheduler::thread_state::RUNNING;
    to->last_cpu = pcpu->cpu_id;
    pcpu->current_thread = to;

    uintptr_t new_kstack = to->stack_base + to->stack_size;
    pcpu->kernel_stack = new_kstack;
    g_current_kernel_stack = new_kstack;

#ifdef __x86_64__
    arch::amd64::tss_set_rsp0(new_kstack);
#endif
    if (to->pml4_phys && to->pml4_phys != from->pml4_phys) {
        kernel::memory::vmm::switch_to(to->pml4_phys);
    }

    switch_context(&from->stack_pointer, to->stack_pointer);
}

long sys_ipc_call(uint32_t target_tid, fast_msg* regs) noexcept {
    uintptr_t flags = irq_save();

    auto* sender = cpu::this_cpu()->current_thread;
    auto* receiver = scheduler::scheduler::get_thread_by_tid(target_tid);

    if (!receiver) {
        irq_restore(flags);
        return -1;
    }

    // Copy message into sender's register buffer
    sender->ipc_regs[0] = regs->type;
    sender->ipc_regs[1] = regs->d0;
    sender->ipc_regs[2] = regs->d1;
    sender->ipc_regs[3] = regs->d2;

    // Is the receiver waiting for us?
    if (receiver->ipc_waiting) {
        // Fast path: direct switch to receiver
        receiver->ipc_waiting = false;

        // Transfer message to receiver's register buffer
        receiver->ipc_regs[0] = regs->type;
        receiver->ipc_regs[1] = regs->d0;
        receiver->ipc_regs[2] = regs->d1;
        receiver->ipc_regs[3] = regs->d2;

        // Record that sender is waiting for a reply
        receiver->ipc_caller = sender;
        sender->state = scheduler::thread_state::BLOCKED;

        // Direct switch: sender → receiver (no scheduler)
        direct_switch(sender, receiver);

        // When we get here, the receiver has replied.
        // Our ipc_regs now contain the reply.
        regs->type = sender->ipc_regs[0];
        regs->d0 = sender->ipc_regs[1];
        regs->d1 = sender->ipc_regs[2];
        regs->d2 = sender->ipc_regs[3];

        irq_restore(flags);
        return 0;
    }

    // Slow path: receiver is not waiting — queue and block
    receiver->ipc_caller = sender;
    sender->state = scheduler::thread_state::BLOCKED;

    // Use the scheduler to block — the receiver will pick us up
    // when it calls ipc_wait()
    irq_restore(flags);
    scheduler::scheduler::schedule();

    // Woken by reply — read response
    flags = irq_save();
    regs->type = sender->ipc_regs[0];
    regs->d0 = sender->ipc_regs[1];
    regs->d1 = sender->ipc_regs[2];
    regs->d2 = sender->ipc_regs[3];
    irq_restore(flags);
    return 0;
}

long sys_ipc_reply(fast_msg* regs) noexcept {
    uintptr_t flags = irq_save();

    auto* server = cpu::this_cpu()->current_thread;
    auto* caller = server->ipc_caller;

    if (!caller) {
        irq_restore(flags);
        return -1; // No pending caller
    }

    server->ipc_caller = nullptr;

    // Transfer reply into caller's register buffer
    caller->ipc_regs[0] = regs->type;
    caller->ipc_regs[1] = regs->d0;
    caller->ipc_regs[2] = regs->d1;
    caller->ipc_regs[3] = regs->d2;

    // Direct switch: server → caller (caller resumes in ipc_call)
    caller->state = scheduler::thread_state::RUNNING;
    server->state = scheduler::thread_state::READY;

    // Put server back on the run queue
    scheduler::scheduler::add_thread(server);

    // Direct switch to caller
    direct_switch(server, caller);

    irq_restore(flags);
    return 0;
}

long sys_ipc_wait(fast_msg* regs) noexcept {
    uintptr_t flags = irq_save();

    auto* server = cpu::this_cpu()->current_thread;

    // Check if a caller is already queued
    if (server->ipc_caller) {
        auto* caller = server->ipc_caller;
        // Copy caller's message to our regs
        regs->type = caller->ipc_regs[0];
        regs->d0 = caller->ipc_regs[1];
        regs->d1 = caller->ipc_regs[2];
        regs->d2 = caller->ipc_regs[3];

        irq_restore(flags);
        return 0;
    }

    // No caller yet — block
    server->ipc_waiting = true;
    server->state = scheduler::thread_state::BLOCKED;
    irq_restore(flags);
    scheduler::scheduler::schedule();

    // Woken by ipc_call direct switch — ipc_regs already populated
    flags = irq_save();
    regs->type = server->ipc_regs[0];
    regs->d0 = server->ipc_regs[1];
    regs->d1 = server->ipc_regs[2];
    regs->d2 = server->ipc_regs[3];
    irq_restore(flags);
    return 0;
}

} // namespace kernel::ipc
