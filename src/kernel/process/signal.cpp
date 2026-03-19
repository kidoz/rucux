// SPDX-License-Identifier: MIT
#include <kernel/process/signal.hpp>

namespace kernel::process {

int signal_manager::sys_sigaction(int signum, const void* act, void* oldact) noexcept {
    (void)signum;
    (void)act;
    (void)oldact;
    return -1; // ENOSYS
}

int signal_manager::sys_kill(int pid, int sig) noexcept {
    (void)pid;
    (void)sig;
    return -1; // ENOSYS
}

int signal_manager::sys_sigprocmask(int how, const void* set, void* oldset) noexcept {
    (void)how;
    (void)set;
    (void)oldset;
    return -1; // ENOSYS
}

} // namespace kernel::process