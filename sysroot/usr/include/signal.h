// SPDX-License-Identifier: MIT
#ifndef _SIGNAL_H
#define _SIGNAL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int sig_atomic_t;
typedef void (*sighandler_t)(int);

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t) - 1)

#define SIGINT 2
#define SIGILL 4
#define SIGABRT 6
#define SIGFPE 8
#define SIGSEGV 11
#define SIGTERM 11
#define SIGALRM 14
#define SIGUSR1 10
#define SIGUSR2 12
#define SIGKILL 9
#define SIGWINCH 28

sighandler_t signal(int signum, sighandler_t handler);
int raise(int sig);

typedef unsigned int sigset_t;
struct sigaction {
    sighandler_t sa_handler;
    sigset_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

int sigaction(int signum, const struct sigaction* act, struct sigaction* oldact);
int sigemptyset(sigset_t* set);
int sigfillset(sigset_t* set);
int sigaddset(sigset_t* set, int signum);
int sigdelset(sigset_t* set, int signum);
int sigismember(const sigset_t* set, int signum);

#ifdef __cplusplus
}
#endif

#endif // _SIGNAL_H