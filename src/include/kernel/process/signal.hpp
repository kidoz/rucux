// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <lib/stddef.hpp>

namespace kernel::process {

class signal_manager {
public:
    static int sys_sigaction(int signum, const void* act, void* oldact) noexcept;
    static int sys_kill(int pid, int sig) noexcept;
    static int sys_sigprocmask(int how, const void* set, void* oldset) noexcept;
};

} // namespace kernel::process