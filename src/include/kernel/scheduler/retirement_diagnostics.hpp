// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

#ifdef RUCUX_VM_SELF_TEST
namespace kernel::scheduler::retirement {
enum event : uint32_t {
    round_start = 1,
    phase,
    collect_begin,
    collect_result,
    server_wait,
    cancelled_wait,
    cancel_ack,
    server_next,
    server_reply,
    call_result,
    cancel_result,
    enqueue,
    claim,
    off_cpu,
    exit_status,
    wait_enroll,
    wait_result,
    ipc_complete,
};
// Leaf lock only: no allocation, printing, blocking or retained thread pointers.
void note(event kind, uint32_t tid, uint64_t a = 0, uint64_t b = 0) noexcept;
void dump() noexcept;
} // namespace kernel::scheduler::retirement
#define RETIREMENT_NOTE(kind, tid, a, b)                                                                               \
    ::kernel::scheduler::retirement::note(::kernel::scheduler::retirement::kind, tid, static_cast<uint64_t>(a),        \
                                          static_cast<uint64_t>(b))
#else
#define RETIREMENT_NOTE(kind, tid, a, b) ((void)0)
#endif
