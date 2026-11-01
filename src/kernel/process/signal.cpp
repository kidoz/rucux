// SPDX-License-Identifier: MIT
#include <kernel/ipc/ipc.hpp>
#include <kernel/process/credentials.hpp>
#include <kernel/process/signal.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/vfs/vfs.hpp>
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
static constexpr int SIG_IGN_VAL = 1;
static constexpr int SIGKILL_VAL = 9;
static constexpr int SIGTERM_VAL = 15;

int signal_manager::sys_sigaction(int signum, const void* act, void* oldact) noexcept {
    if (signum < 1 || signum >= scheduler::thread::MAX_SIGNALS) return -1; // EINVAL
    // Can't catch SIGKILL(9) or SIGSTOP(19)
    if (signum == 9 || signum == 19) return -1;

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
        t->sig_restorers[signum] = new_act->sa_restorer;
    }

    return 0;
}

int signal_manager::sys_kill(int pid, int sig) noexcept {
    if (sig < 0 || sig >= scheduler::thread::MAX_SIGNALS) return -1;

    return scheduler::scheduler::signal_thread(pid, sig);
}

int signal_manager::post_signal(scheduler::thread* target, int sig) noexcept {
    if (!target || sig < 0 || sig >= scheduler::thread::MAX_SIGNALS) return -22;
    irq_lock_guard guard(target->wait_lock);
    if (target->state.load(kernel::acquire) == scheduler::thread_state::TERMINATED) return -3;
    if (!sig) return 0;
    if ((sig == SIGKILL_VAL || sig == SIGTERM_VAL) && !target->user_entry) return -1;
    if (sig != SIGKILL_VAL &&
        target->sig_handlers[sig] == reinterpret_cast<scheduler::thread::sighandler_t>(SIG_IGN_VAL))
        return 0;
    target->sig_pending.fetch_or(1U << sig, release);
    // Only explicitly enrolled waits may be interrupted; their kernel scopes
    // resume and unlink queue records before fatal delivery.
    if (target->fatal_signal_pending() && target->interruptible_wait &&
        target->state.load(kernel::acquire) == scheduler::thread_state::BLOCKED)
        scheduler::scheduler::add_thread(target);
    return 0;
}

int signal_manager::sys_sigprocmask(int how, const void* set, void* oldset) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    if (!t) return -1;

    if (oldset) {
        *reinterpret_cast<uint32_t*>(oldset) = t->sig_mask.load(relaxed);
    }

    if (set) {
        uint32_t new_mask = *reinterpret_cast<const uint32_t*>(set);
        switch (how) {
        case 0:
            t->sig_mask.fetch_or(new_mask, relaxed);
            break; // SIG_BLOCK
        case 1:
            t->sig_mask.fetch_and(~new_mask, relaxed);
            break; // SIG_UNBLOCK
        case 2:
            t->sig_mask.store(new_mask, relaxed);
            break; // SIG_SETMASK
        default:
            return -1;
        }
        // Never allow masking SIGKILL or SIGSTOP
        t->sig_mask.fetch_and(~((1U << 9) | (1U << 19)), relaxed);
    }

    return 0;
}

bool signal_manager::consume_fatal_signal(scheduler::thread* t) noexcept {
    if (!t || !t->user_entry || t->state.load(kernel::acquire) == scheduler::thread_state::TERMINATED ||
        t->kernel_depth.load(acquire) != 0)
        return false;

    const uint32_t mask = t->sig_mask.load(relaxed) & ~(1U << SIGKILL_VAL);
    uint32_t pending = t->sig_pending.load(acquire) & ~mask;
    int fatal_sig = 0;

    if (pending & (1U << SIGKILL_VAL)) {
        fatal_sig = SIGKILL_VAL;
    } else if (pending & (1U << SIGTERM_VAL)) {
        fatal_sig = SIGTERM_VAL;
    }

    if (fatal_sig == 0) return false;

    // Fatal delivery, like exit(), releases descriptors before making the
    // thread reapable. No global thread-list lock is held here.
    ipc::detach_thread(t);
    vfs::vfs_manager::close_all(t);
    t->sig_pending.fetch_and(~(1U << fatal_sig), acq_rel);
    scheduler::scheduler::publish_termination(t, fatal_sig & 0x7f);
    return true;
}

} // namespace kernel::process
