// SPDX-License-Identifier: MIT
#pragma once
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::ipc {

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

} // namespace kernel::ipc
