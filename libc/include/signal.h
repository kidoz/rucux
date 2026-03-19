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

#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGABRT 6
#define SIGFPE 8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGWINCH 28
#define SIGBUS 7

#define BUS_ADRALN 1
#define BUS_ADRERR 2
#define BUS_OBJERR 3

typedef struct {
    int si_signo;
    int si_errno;
    int si_code;
    void *si_addr;
} siginfo_t;

sighandler_t signal(int signum, sighandler_t handler);
int raise(int sig);

typedef unsigned int sigset_t;
struct sigaction {
    union {
        sighandler_t sa_handler;
        void (*sa_sigaction)(int, siginfo_t *, void *);
    };
    sigset_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#define SA_NOCLDSTOP 1
#define SA_NOCLDWAIT 2
#define SA_SIGINFO   4
#define SA_ONSTACK   0x08000000
#define SA_RESTART   0x10000000
#define SA_NODEFER   0x40000000
#define SA_RESETHAND 0x80000000

int sigaction(int signum, const struct sigaction* act, struct sigaction* oldact);
int kill(int pid, int sig);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int siginterrupt(int sig, int flag);
int sigemptyset(sigset_t* set);
int sigfillset(sigset_t* set);
int sigaddset(sigset_t* set, int signum);
int sigdelset(sigset_t* set, int signum);
int sigismember(const sigset_t* set, int signum);

#ifdef __cplusplus
}
#endif

#endif // _SIGNAL_H
