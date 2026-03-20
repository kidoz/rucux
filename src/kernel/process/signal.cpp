// SPDX-License-Identifier: MIT
#include <kernel/process/signal.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <lib/string.hpp>

namespace kernel::process {

// POSIX sigaction struct layout (must match libc/include/signal.h)
struct kernel_sigaction {
    union {
        void (*sa_handler)(int);
        void (*sa_sigaction)(int, void*, void*);
    };
    uint32_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};

// Signal constants
static constexpr int SIG_DFL_VAL = 0;
static constexpr int SIG_IGN_VAL = 1;

int signal_manager::sys_sigaction(int signum, const void* act, void* oldact) noexcept {
    if (signum < 1 || signum >= scheduler::thread::MAX_SIGNALS)
        return -1; // EINVAL
    // Can't catch SIGKILL(9) or SIGSTOP(19)
    if (signum == 9 || signum == 19)
        return -1;

    auto* t = scheduler::scheduler::current_thread();
    if (!t) return -1;

    if (oldact) {
        auto* old = reinterpret_cast<kernel_sigaction*>(oldact);
        old->sa_handler = t->sig_handlers[signum];
        old->sa_mask = 0;
        old->sa_flags = 0;
        old->sa_restorer = nullptr;
    }

    if (act) {
        auto* new_act = reinterpret_cast<const kernel_sigaction*>(act);
        t->sig_handlers[signum] = new_act->sa_handler;
    }

    return 0;
}

int signal_manager::sys_kill(int pid, int sig) noexcept {
    if (sig < 0 || sig >= scheduler::thread::MAX_SIGNALS)
        return -1;

    // pid == 0 or pid == current tid → send to self
    auto* t = scheduler::scheduler::current_thread();
    if (!t) return -1;

    scheduler::thread* target = nullptr;
    if (pid == 0 || static_cast<uint32_t>(pid) == t->tid)
        target = t;
    else
        target = scheduler::scheduler::get_thread_by_tid(static_cast<uint32_t>(pid));

    if (!target) return -1; // ESRCH

    if (sig == 0) return 0; // Signal 0 = check permission only

    // Check if signal is ignored
    auto handler = target->sig_handlers[sig];
    if (handler == reinterpret_cast<scheduler::thread::sighandler_t>(SIG_IGN_VAL))
        return 0; // Silently ignored

    // Set pending bit
    target->sig_pending |= (1U << sig);

    // If target is blocked, unblock it so it can process the signal
    if (target->state == scheduler::thread_state::BLOCKED) {
        scheduler::scheduler::unblock(target);
    }

    return 0;
}

int signal_manager::sys_sigprocmask(int how, const void* set, void* oldset) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    if (!t) return -1;

    if (oldset) {
        *reinterpret_cast<uint32_t*>(oldset) = t->sig_mask;
    }

    if (set) {
        uint32_t new_mask = *reinterpret_cast<const uint32_t*>(set);
        switch (how) {
        case 0: t->sig_mask |= new_mask; break;  // SIG_BLOCK
        case 1: t->sig_mask &= ~new_mask; break;  // SIG_UNBLOCK
        case 2: t->sig_mask = new_mask; break;     // SIG_SETMASK
        default: return -1;
        }
        // Never allow masking SIGKILL or SIGSTOP
        t->sig_mask &= ~((1U << 9) | (1U << 19));
    }

    return 0;
}

} // namespace kernel::process
