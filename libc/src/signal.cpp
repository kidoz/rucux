// SPDX-License-Identifier: MIT
#include <signal.h>

extern "C" {

sighandler_t signal(int signum, sighandler_t handler) {
    (void)signum; (void)handler;
    return SIG_ERR; // stub
}

int raise(int sig) {
    (void)sig;
    return -1; // stub
}

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
    (void)signum; (void)act; (void)oldact;
    return -1; // stub
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
