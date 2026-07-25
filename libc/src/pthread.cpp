// SPDX-License-Identifier: MIT
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <sys/mman.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>

extern "C" {

static long __syscall(long num, long a1 = 0, long a2 = 0, long a3 = 0, long a4 = 0, long a5 = 0, long a6 = 0) {
    long ret;
    register long r10 asm("r10") = a4;
    register long r8 asm("r8") = a5;
    register long r9 asm("r9") = a6;
    asm volatile("syscall"
                 : "=a"(ret)
                 : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
                 : "rcx", "r11", "memory");
    return ret;
}

int sched_yield(void) {
    __syscall(SYS_YIELD);
    return 0;
}

// ─── Thread creation ───────────────────────────────────────────────────────

struct thread_start_info {
    void* (*start_routine)(void*);
    void* arg;
    int started;
};

static void thread_trampoline(thread_start_info* info) {
    void* (*fn)(void*) = info->start_routine;
    void* arg = info->arg;
    __sync_fetch_and_add(&info->started, 1);
    fn(arg);
    __syscall(SYS_EXIT, 0);
}

int pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start_routine)(void*), void* arg) {
    (void)attr;
    size_t stack_size = 64 * 1024;
    void* stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack == MAP_FAILED) return -1;

    thread_start_info info = {start_routine, arg, 0};
    long tid = __syscall(SYS_CLONE, (long)thread_trampoline, (long)((uintptr_t)stack + stack_size), (long)&info);
    if (tid < 0) {
        munmap(stack, stack_size);
        return -1;
    }
    if (thread) *thread = (pthread_t)tid;
    while (info.started == 0)
        sched_yield();
    return 0;
}

int pthread_join(pthread_t thread, void** retval) {
    (void)retval;
    // Busy-yield until target thread exits (kill(tid,0) fails for dead threads)
    while (__syscall(SYS_KILL, (long)thread, 0) >= 0)
        sched_yield();
    return 0;
}

int pthread_detach(pthread_t) {
    return 0;
}
int pthread_attr_init(pthread_attr_t* a) {
    if (a) *a = 0;
    return 0;
}
int pthread_attr_destroy(pthread_attr_t*) {
    return 0;
}

// ─── Mutex ─────────────────────────────────────────────────────────────────

int pthread_mutex_init(pthread_mutex_t* m, const pthread_mutexattr_t*) {
    *m = 0;
    return 0;
}
int pthread_mutex_destroy(pthread_mutex_t* m) {
    *m = 0;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t* m) {
    while (__sync_val_compare_and_swap(m, 0, 1) != 0)
        __syscall(SYS_FUTEX, (long)m, 0, 1);
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t* m) {
    return (__sync_val_compare_and_swap(m, 0, 1) == 0) ? 0 : 16;
}

int pthread_mutex_unlock(pthread_mutex_t* m) {
    *m = 0;
    __syscall(SYS_FUTEX, (long)m, 1, 1);
    return 0;
}

// ─── Condition Variables ───────────────────────────────────────────────────

int pthread_cond_init(pthread_cond_t* c, const pthread_condattr_t*) {
    c->seq = 0;
    c->mtx = nullptr;
    return 0;
}
int pthread_cond_destroy(pthread_cond_t*) {
    return 0;
}

int pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m) {
    uint32_t seq = c->seq;
    pthread_mutex_unlock(m);
    __syscall(SYS_FUTEX, (long)&c->seq, 0, seq); // FUTEX_WAIT
    pthread_mutex_lock(m);
    return 0;
}

int pthread_cond_signal(pthread_cond_t* c) {
    __sync_fetch_and_add(&c->seq, 1);
    __syscall(SYS_FUTEX, (long)&c->seq, 1, 1);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t* c) {
    __sync_fetch_and_add(&c->seq, 1);
    __syscall(SYS_FUTEX, (long)&c->seq, 1, 0x7FFFFFFF);
    return 0;
}

int pthread_cond_timedwait(pthread_cond_t* c, pthread_mutex_t* m, const void*) {
    return pthread_cond_wait(c, m); // Timeout not implemented
}

// ─── Read-Write Locks ──────────────────────────────────────────────────────

int pthread_rwlock_init(pthread_rwlock_t* rw, const pthread_rwlockattr_t*) {
    pthread_mutex_init(&rw->lock, nullptr);
    rw->readers = 0;
    rw->writer = 0;
    return 0;
}
int pthread_rwlock_destroy(pthread_rwlock_t* rw) {
    pthread_mutex_destroy(&rw->lock);
    return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t* rw) {
    pthread_mutex_lock(&rw->lock);
    while (rw->writer) {
        pthread_mutex_unlock(&rw->lock);
        sched_yield();
        pthread_mutex_lock(&rw->lock);
    }
    rw->readers++;
    pthread_mutex_unlock(&rw->lock);
    return 0;
}

int pthread_rwlock_wrlock(pthread_rwlock_t* rw) {
    pthread_mutex_lock(&rw->lock);
    while (rw->writer || rw->readers) {
        pthread_mutex_unlock(&rw->lock);
        sched_yield();
        pthread_mutex_lock(&rw->lock);
    }
    rw->writer = 1;
    pthread_mutex_unlock(&rw->lock);
    return 0;
}

int pthread_rwlock_unlock(pthread_rwlock_t* rw) {
    pthread_mutex_lock(&rw->lock);
    if (rw->writer)
        rw->writer = 0;
    else if (rw->readers > 0)
        rw->readers--;
    pthread_mutex_unlock(&rw->lock);
    return 0;
}

// ─── Thread-specific data ──────────────────────────────────────────────────

#define MAX_KEYS 64
static void* g_tsd[MAX_KEYS] = {};
static int g_tsd_used[MAX_KEYS] = {};
static int g_next_key = 0;

int pthread_key_create(pthread_key_t* key, void (*)(void*)) {
    if (g_next_key >= MAX_KEYS) return -1;
    *key = (pthread_key_t)g_next_key;
    g_tsd_used[g_next_key] = 1;
    g_tsd[g_next_key++] = nullptr;
    return 0;
}
int pthread_key_delete(pthread_key_t key) {
    if (key < MAX_KEYS) {
        g_tsd_used[key] = 0;
        g_tsd[key] = nullptr;
    }
    return 0;
}
void* pthread_getspecific(pthread_key_t key) {
    return (key < MAX_KEYS && g_tsd_used[key]) ? g_tsd[key] : nullptr;
}
int pthread_setspecific(pthread_key_t key, const void* val) {
    if (key >= MAX_KEYS || !g_tsd_used[key]) return -1;
    g_tsd[key] = (void*)val;
    return 0;
}

// ─── Misc ──────────────────────────────────────────────────────────────────

pthread_t pthread_self(void) {
    return 1;
}
int pthread_equal(pthread_t a, pthread_t b) {
    return a == b;
}

int pthread_once(pthread_once_t* ctl, void (*fn)(void)) {
    if (!ctl || !fn) return -1;
    if (__sync_val_compare_and_swap(ctl, 0, 1) == 0) fn();
    return 0;
}

int pthread_setcancelstate(int, int* old) {
    if (old) *old = 0;
    return 0;
}
int pthread_setcanceltype(int, int* old) {
    if (old) *old = 0;
    return 0;
}

} // extern "C"
