// SPDX-License-Identifier: MIT
#include <pthread.h>
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

struct thread_start_info {
    void* (*start_routine)(void*);
    void* arg;
    pthread_t* tid_ptr;
    int started;
};

// The entry point for the new thread
static void thread_trampoline(thread_start_info* info) {
    void* (*start_routine)(void*) = info->start_routine;
    void* arg = info->arg;

    // Signal the parent that we have copied the args
    __sync_fetch_and_add(&info->started, 1);

    start_routine(arg);

    // We should call a sys_thread_exit here, but for now we just exit the whole process.
    // A real implementation needs a thread-specific exit to not kill the whole process.
    __syscall(SYS_EXIT, 0);
}

int pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start_routine)(void*), void* arg) {
    (void)attr;

    // Allocate a stack for the new thread (e.g., 64KB)
    size_t stack_size = 64 * 1024;
    void* stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack == MAP_FAILED) {
        return -1;
    }

    // The stack grows downwards
    void* stack_top = (void*)((uintptr_t)stack + stack_size);

    thread_start_info info;
    info.start_routine = start_routine;
    info.arg = arg;
    info.tid_ptr = thread;
    info.started = 0;

    long tid = __syscall(SYS_CLONE, (long)thread_trampoline, (long)stack_top, (long)&info);
    if (tid < 0) {
        munmap(stack, stack_size);
        return -1;
    }

    if (thread) {
        *thread = (pthread_t)tid;
    }

    // Wait for the child to copy the info before we return and destroy it
    while (info.started == 0) {
        sched_yield();
    }

    return 0;
}

int pthread_join(pthread_t thread, void** retval) {
    (void)thread;
    (void)retval;
    // Basic stub, real join requires a mechanism to track thread exits
    return 0;
}

int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr) {
    (void)attr;
    *mutex = 0;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t* mutex) {
    *mutex = 0;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t* mutex) {
    while (__sync_val_compare_and_swap(mutex, 0, 1) != 0) {
        // FUTEX_WAIT: Wait if the value is still 1
        __syscall(SYS_FUTEX, (long)mutex, 0, 1);
    }
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t* mutex) {
    *mutex = 0;
    // FUTEX_WAKE: Wake up 1 waiting thread
    __syscall(SYS_FUTEX, (long)mutex, 1, 1);
    return 0;
}

pthread_t pthread_self(void) {
    return 1; // stub
}

int pthread_equal(pthread_t t1, pthread_t t2) {
    return t1 == t2;
}

int pthread_once(pthread_once_t* once_control, void (*init_routine)(void)) {
    if (!once_control || !init_routine) return -1;
    if (*once_control == 0) {
        init_routine();
        *once_control = 1;
    }
    return 0;
}

} // extern "C"
