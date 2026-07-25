// SPDX-License-Identifier: MIT
#pragma once
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::ipc {

// ─── Legacy message (memory-based) ────────────────────────────────────────

struct message {
    uint32_t sender;
    uint32_t type;
    uint64_t data[4];
};

class ipc_manager {
public:
    static void send_sync(uint32_t target_tid, const message& msg) noexcept;
    static void send_async(uint32_t target_tid, const message& msg) noexcept;
    static void receive_sync(message& msg) noexcept;
};

// ─── Fast IPC (register-based, direct thread switch) ──────────────────────

// A fast message fits in registers: type + 3 data words.
// On amd64: passed in rdi/rsi/rdx/r10 (matching syscall ABI).
// No memory copy required — data stays in registers through the entire path.
struct fast_msg {
    uint64_t type; // Message type / opcode
    uint64_t d0;   // Data register 0
    uint64_t d1;   // Data register 1
    uint64_t d2;   // Data register 2
};

// sys_ipc_call: Send a fast message to target_tid, block until reply.
//   - If receiver is blocked in ipc_wait, do a direct context switch
//     (sender → receiver, no scheduler involvement)
//   - On return, regs contain the reply message
//   Returns 0 on success, -1 on error (target not found, etc.)
long sys_ipc_call(uint32_t target_tid, fast_msg* regs) noexcept;

// sys_ipc_reply: Send a reply to the thread that called us, resume it.
//   - Direct context switch back to the caller
//   Returns 0 on success, -1 if no pending caller
long sys_ipc_reply(fast_msg* regs) noexcept;

// sys_ipc_wait: Block until an IPC call arrives.
//   - On return, regs contain the received message and regs->type has sender tid
//     encoded in upper 32 bits
//   Returns 0 on success
long sys_ipc_wait(fast_msg* regs) noexcept;

} // namespace kernel::ipc
