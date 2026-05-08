// SPDX-License-Identifier: MIT
#pragma once
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::scheduler {
struct thread;
}

namespace kernel::process {

class signal_manager {
public:
    static int sys_sigaction(int signum, const void* act, void* oldact) noexcept;
    static int sys_kill(int pid, int sig) noexcept;
    static int sys_sigprocmask(int how, const void* set, void* oldset) noexcept;
    static bool consume_fatal_signal(kernel::scheduler::thread* t) noexcept;
};

} // namespace kernel::process
