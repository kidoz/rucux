// SPDX-License-Identifier: MIT
#include <kernel/ipc/ipc.hpp>
#include <kernel/scheduler/retirement_diagnostics.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/sync/spinlock.hpp>
#include <lib/string.hpp>

namespace kernel::ipc {
namespace {
using scheduler::ipc_wait_kind;
using scheduler::thread;
using scheduler::thread_state;
irq_spinlock ipc_lock;
constexpr int PENDING = -115;

// Lock order: thread-list (lookup/exit) -> IPC -> per-thread wait -> run queue.
// Lookup obtains a pin before taking IPC; unpin never takes the thread-list lock.
class peer_pin {
public:
    explicit peer_pin(thread* peer) noexcept
        : peer_(peer) {}
    ~peer_pin() {
        if (peer_) peer_->unpin();
    }
    peer_pin(const peer_pin&) = delete;
    peer_pin& operator=(const peer_pin&) = delete;
    thread* get() const noexcept { return peer_; }
    thread* detach() noexcept {
        auto* result = peer_;
        peer_ = nullptr;
        return result;
    }

private:
    thread* peer_;
};

bool closed(const thread* owner) noexcept {
    return owner->ipc_closed.load(acquire) || owner->state.load(acquire) == thread_state::TERMINATED;
}
void copy_regs(fast_msg& out, const thread& owner) noexcept {
    out = {owner.ipc_regs[0], owner.ipc_regs[1], owner.ipc_regs[2], owner.ipc_regs[3]};
}
void save_regs(thread& owner, const fast_msg& in) noexcept {
    owner.ipc_regs[0] = in.type;
    owner.ipc_regs[1] = in.d0;
    owner.ipc_regs[2] = in.d1;
    owner.ipc_regs[3] = in.d2;
}
void unlink_request(thread* peer, thread* sender) noexcept {
    thread* previous = nullptr;
    auto** link = &peer->send_queue_head;
    while (*link && *link != sender) {
        previous = *link;
        link = &(*link)->send_queue_next;
    }
    if (!*link) return; // The pin may instead belong to an accepted call.
    *link = sender->send_queue_next;
    if (peer->send_queue_tail == sender) peer->send_queue_tail = previous;
    sender->send_queue_next = nullptr;
    --peer->ipc_queue_count;
}

// Caller holds IPC. The peer's queue or accepted-call slot owns one sender pin;
// the sender owns one peer pin. Wake before dropping those pins, with no raw
// pointer escaping the lock. A resuming sender must take IPC before returning.
void complete(thread* sender, int result, bool cancelled = false) noexcept {
    auto* peer = sender->ipc_peer;
    if (!peer) return;
    if (peer->ipc_caller == sender) {
        peer->ipc_caller = nullptr;
        if (cancelled && !closed(peer)) peer->ipc_reply_cancelled = true;
    } else {
        unlink_request(peer, sender);
    }
    sender->ipc_peer = nullptr;
    sender->has_queued_msg = false;
    sender->ipc_result = result;
    RETIREMENT_NOTE(ipc_complete, sender->tid, peer->tid, static_cast<uint32_t>(result));
    scheduler::scheduler::unblock(sender);
    peer->unpin();
    sender->unpin();
}
thread* first_request(thread* receiver, ipc_wait_kind kind) noexcept {
    for (;;) {
        auto* sender = receiver->send_queue_head;
        while (sender && sender->ipc_kind != kind)
            sender = sender->send_queue_next;
        if (!sender) return nullptr;
        if (!sender->fatal_signal_pending()) return sender;
        complete(sender, -4, true);
    }
}
void clear_wait(thread* owner) noexcept {
    owner->ipc_kind = ipc_wait_kind::none;
    owner->ipc_waiting = false;
    owner->recv_buffer = nullptr;
    owner->end_interruptible_wait();
}

int submit(uint32_t tid, ipc_wait_kind kind, const message* legacy, const fast_msg* fixed, fast_msg* reply) noexcept {
    auto* sender = scheduler::scheduler::current_thread();
    if (!sender) return -3;
    if (sender->tid == tid) return -35;
    peer_pin target(scheduler::scheduler::pin_thread(tid));
    if (!target.get()) return -3;
    {
        irq_lock_guard guard(ipc_lock);
        auto* peer = target.get();
        if (closed(sender) || closed(peer)) return -32;
        if (!process::may_ipc(sender->creds, peer->creds)) return -1;
        if (sender->ipc_kind != ipc_wait_kind::none) return -16;
        if (peer->ipc_queue_count >= MAX_PENDING_REQUESTS) return -11;
        if (!sender->begin_interruptible_wait()) return -4;
        sender->ipc_kind = kind;
        sender->ipc_result = PENDING;
        sender->ipc_peer = target.detach();
        sender->send_queue_next = nullptr;
        if (legacy) {
            sender->queued_msg.sender = sender->tid;
            sender->queued_msg.type = legacy->type;
            lib::memcpy(sender->queued_msg.data, legacy->data, sizeof(legacy->data));
            sender->has_queued_msg = true;
        } else {
            save_regs(*sender, *fixed);
        }
        sender->pin(); // Ownership of this request transfers to the peer queue.
        if (peer->send_queue_tail)
            peer->send_queue_tail->send_queue_next = sender;
        else
            peer->send_queue_head = sender;
        peer->send_queue_tail = sender;
        ++peer->ipc_queue_count;
        const auto wanted = kind == ipc_wait_kind::call ? ipc_wait_kind::receive_call : ipc_wait_kind::legacy_receive;
        if (peer->ipc_kind == wanted) scheduler::scheduler::unblock(peer);
    }
    for (;;) {
        scheduler::scheduler::schedule();
        irq_lock_guard guard(ipc_lock);
        if (sender->fatal_signal_pending() && sender->ipc_peer) complete(sender, -4, true);
        if (sender->ipc_result != PENDING) {
            const int result = sender->fatal_signal_pending() ? -4 : sender->ipc_result;
            if (!result && reply) copy_regs(*reply, *sender);
            clear_wait(sender);
            return result;
        }
        // Unrelated/spurious wakeups do not fabricate a successful exchange.
        sender->end_interruptible_wait();
        if (!sender->begin_interruptible_wait()) {
            complete(sender, -4, true);
            clear_wait(sender);
            return -4;
        }
    }
}
} // namespace

int ipc_manager::send_sync(uint32_t target_tid, const message& msg) noexcept {
    return submit(target_tid, ipc_wait_kind::legacy_send, &msg, nullptr, nullptr);
}
int ipc_manager::send_async(uint32_t target_tid, const message& msg) noexcept {
    auto* sender = scheduler::scheduler::current_thread();
    if (!sender) return -3;
    peer_pin target(scheduler::scheduler::pin_thread(target_tid));
    if (!target.get()) return -3;
    irq_lock_guard guard(ipc_lock);
    auto* peer = target.get();
    if (closed(sender) || closed(peer)) return -32;
    if (!process::may_ipc(sender->creds, peer->creds)) return -1;
    if (sender->fatal_signal_pending()) return -4;
    const size_t next = (peer->async_tail + 1) % thread::ASYNC_QUEUE_SIZE;
    if (next == peer->async_head) return -11;
    auto& entry = peer->async_queue[peer->async_tail];
    entry.sender = sender->tid;
    entry.type = msg.type;
    lib::memcpy(entry.data, msg.data, sizeof(msg.data));
    peer->async_tail = next;
    if (peer->ipc_kind == ipc_wait_kind::legacy_receive) scheduler::scheduler::unblock(peer);
    return 0;
}
int ipc_manager::receive_sync(message& msg) noexcept {
    auto* receiver = scheduler::scheduler::current_thread();
    if (!receiver) return -3;
    for (;;) {
        {
            irq_lock_guard guard(ipc_lock);
            if (closed(receiver)) return -32;
            if (receiver->fatal_signal_pending()) return -4;
            if (receiver->ipc_kind != ipc_wait_kind::none) return -16;
            if (receiver->async_head != receiver->async_tail) {
                const auto& entry = receiver->async_queue[receiver->async_head];
                msg.sender = entry.sender;
                msg.type = entry.type;
                lib::memcpy(msg.data, entry.data, sizeof(msg.data));
                receiver->async_head = (receiver->async_head + 1) % thread::ASYNC_QUEUE_SIZE;
                return 0;
            }
            if (auto* sender = first_request(receiver, ipc_wait_kind::legacy_send)) {
                msg.sender = sender->tid;
                msg.type = sender->queued_msg.type;
                lib::memcpy(msg.data, sender->queued_msg.data, sizeof(msg.data));
                complete(sender, 0);
                return 0;
            }
            if (!receiver->begin_interruptible_wait()) return -4;
            receiver->ipc_kind = ipc_wait_kind::legacy_receive;
        }
        scheduler::scheduler::schedule();
        irq_lock_guard guard(ipc_lock);
        clear_wait(receiver);
    }
}
long sys_ipc_call(uint32_t target_tid, fast_msg* regs) noexcept {
    if (!regs) return -14;
    return submit(target_tid, ipc_wait_kind::call, nullptr, regs, regs);
}
long sys_ipc_wait(fast_msg* regs) noexcept {
    if (!regs) return -14;
    auto* server = scheduler::scheduler::current_thread();
    if (!server) return -3;
    for (;;) {
        {
            irq_lock_guard guard(ipc_lock);
            if (closed(server)) return -32;
            if (server->fatal_signal_pending()) return -4;
            if (server->ipc_kind != ipc_wait_kind::none || server->ipc_caller) return -16;
            if (server->ipc_reply_cancelled) return -125;
            if (auto* caller = first_request(server, ipc_wait_kind::call)) {
                unlink_request(server, caller); // Transfer the queue pin to ipc_caller.
                server->ipc_caller = caller;
                copy_regs(*regs, *caller);
                return 0;
            }
            if (!server->begin_interruptible_wait()) return -4;
            server->ipc_kind = ipc_wait_kind::receive_call;
            server->ipc_waiting = true;
        }
        scheduler::scheduler::schedule();
        irq_lock_guard guard(ipc_lock);
        clear_wait(server);
    }
}
long sys_ipc_reply(fast_msg* regs) noexcept {
    if (!regs) return -14;
    auto* server = scheduler::scheduler::current_thread();
    if (!server) return -3;
    irq_lock_guard guard(ipc_lock);
    if (closed(server)) return -32;
    if (server->fatal_signal_pending()) return -4;
    if (server->ipc_kind != ipc_wait_kind::none) return -16;
    if (server->ipc_reply_cancelled) {
        server->ipc_reply_cancelled = false;
        return -125;
    }
    auto* caller = server->ipc_caller;
    if (!caller) return -2;
    if (caller->fatal_signal_pending()) {
        complete(caller, -4, true);
        server->ipc_reply_cancelled = false; // This reply acknowledges cancellation.
        return -125;
    }
    save_regs(*caller, *regs);
    complete(caller, 0);
    return 0;
}
void detach_thread(thread* owner) noexcept {
    if (!owner) return;
    irq_lock_guard guard(ipc_lock);
    if (owner->ipc_closed.exchange(true, acq_rel)) return;
    if (owner->ipc_peer) complete(owner, -4, true);
    while (owner->send_queue_head)
        complete(owner->send_queue_head, -32);
    if (owner->ipc_caller) complete(owner->ipc_caller, -32);
    owner->ipc_reply_cancelled = false;
    owner->async_head = owner->async_tail = 0;
    owner->recv_buffer = nullptr;
    if (owner->ipc_kind == ipc_wait_kind::legacy_receive || owner->ipc_kind == ipc_wait_kind::receive_call)
        scheduler::scheduler::unblock(owner);
}
#ifdef RUCUX_VM_SELF_TEST
debug_state inspect(const scheduler::thread* owner) noexcept {
    irq_lock_guard guard(ipc_lock);
    return {static_cast<uint32_t>(owner->ipc_kind),
            owner->ipc_peer ? owner->ipc_peer->tid : 0,
            owner->ipc_caller ? owner->ipc_caller->tid : 0,
            static_cast<uint32_t>(owner->ipc_queue_count),
            owner->ipc_reply_cancelled,
            owner->ipc_result};
}
#endif
} // namespace kernel::ipc
