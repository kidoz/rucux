// SPDX-License-Identifier: MIT
#pragma once
#include <lib/stddef.hpp>
#include <stdint.h>
#include <uapi/kernel/ipc.h>

namespace kernel::scheduler {
struct thread;
}

namespace kernel::ipc {

// ─── Legacy message (memory-based) ────────────────────────────────────────

using message = rucux_ipc_message;

class ipc_manager {
public:
    static int send_sync(uint32_t target_tid, const message& msg) noexcept;
    static int send_async(uint32_t target_tid, const message& msg) noexcept;
    static int receive_sync(message& msg) noexcept;
};

// Fixed-size request/reply payload. Syscall arguments point to marshalled buffers;
// the four words are copied into thread-owned storage, never retained by pointer.
using fast_msg = rucux_ipc_words;
inline constexpr size_t MAX_PENDING_REQUESTS = 64;

// Return 0 or negative errno. Calls are FIFO per message kind. A server owns one
// accepted call until reply. A cancelled accepted call leaves a tombstone:
// reply acknowledges it with -ECANCELED before wait can accept another caller.
long sys_ipc_call(uint32_t target_tid, fast_msg* regs) noexcept;
long sys_ipc_reply(fast_msg* regs) noexcept;
long sys_ipc_wait(fast_msg* regs) noexcept;

// Close IPC before publishing termination. Idempotently detach both directions,
// release peer pins, and wake surviving senders/callers with -EPIPE.
void detach_thread(scheduler::thread* owner) noexcept;

} // namespace kernel::ipc
