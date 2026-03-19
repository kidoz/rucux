// SPDX-License-Identifier: MIT
#include <signal.h>
#include <uapi/kernel/syscalls.h>
#include "syscall_impl.h"

extern "C" {

sighandler_t signal(int signum, sighandler_t handler) {
    struct sigaction act, oldact;
    act.sa_handler = handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(signum, &act, &oldact) < 0) return SIG_ERR;
    return oldact.sa_handler;
}

int raise(int sig) {
    return kill(0, sig); // Assume pid 0 means self for now
}

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
    return (int)__syscall(SYS_SIGACTION, (long)signum, (long)act, (long)oldact);
}

int kill(int pid, int sig) {
    return (int)__syscall(SYS_KILL, (long)pid, (long)sig);
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    return (int)__syscall(SYS_SIGPROCMASK, (long)how, (long)set, (long)oldset);
}

int siginterrupt(int sig, int flag) {
    (void)sig; (void)flag;
    return 0; // stub
}

int sigemptyset(sigset_t *set) {
    if (set) *set = 0;
    return 0;
}

int sigfillset(sigset_t *set) {
    if (set) *set = ~0U;
    return 0;
}

int sigaddset(sigset_t *set, int signum) {
    if (set) *set |= (1U << signum);
    return 0;
}

int sigdelset(sigset_t *set, int signum) {
    if (set) *set &= ~(1U << signum);
    return 0;
}

int sigismember(const sigset_t *set, int signum) {
    if (set) return (*set & (1U << signum)) ? 1 : 0;
    return 0;
}

}
