// SPDX-License-Identifier: MIT
#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t pthread_t;
typedef uint32_t pthread_mutex_t;
typedef int pthread_attr_t;
typedef int pthread_mutexattr_t;
typedef int pthread_once_t;

// Condition variable: uses a sequence counter + futex for wait/wake
typedef struct {
    uint32_t seq;         // Sequence counter (incremented on signal/broadcast)
    pthread_mutex_t* mtx; // Associated mutex (set on first wait)
} pthread_cond_t;

typedef int pthread_condattr_t;

// Read-write lock: simple mutex-based (no reader parallelism yet)
typedef struct {
    pthread_mutex_t lock;
    int readers;
    int writer;
} pthread_rwlock_t;

typedef int pthread_rwlockattr_t;
typedef uint32_t pthread_key_t;

#define PTHREAD_MUTEX_INITIALIZER 0
#define PTHREAD_ONCE_INIT 0
#define PTHREAD_COND_INITIALIZER {0, (void*)0}
#define PTHREAD_RWLOCK_INITIALIZER {{0}, 0, 0}

int pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start_routine)(void*), void* arg);
int pthread_join(pthread_t thread, void** retval);

int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);

#define pthread_cleanup_push(routine, arg) do { void (*__cleanup_routine)(void*) = (routine); void* __cleanup_arg = (arg);
#define pthread_cleanup_pop(execute) if (execute) __cleanup_routine(__cleanup_arg); } while (0)

int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr);
int pthread_mutex_destroy(pthread_mutex_t* mutex);
int pthread_mutex_lock(pthread_mutex_t* mutex);
int pthread_mutex_trylock(pthread_mutex_t* mutex);
int pthread_mutex_unlock(pthread_mutex_t* mutex);

pthread_t pthread_self(void);
int pthread_equal(pthread_t t1, pthread_t t2);
int pthread_once(pthread_once_t* once_control, void (*init_routine)(void));

int pthread_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr);
int pthread_cond_destroy(pthread_cond_t* cond);
int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex);
int pthread_cond_signal(pthread_cond_t* cond);
int pthread_cond_broadcast(pthread_cond_t* cond);
int pthread_cond_timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex, const void* abstime);

int pthread_rwlock_init(pthread_rwlock_t* rwlock, const pthread_rwlockattr_t* attr);
int pthread_rwlock_destroy(pthread_rwlock_t* rwlock);
int pthread_rwlock_rdlock(pthread_rwlock_t* rwlock);
int pthread_rwlock_wrlock(pthread_rwlock_t* rwlock);
int pthread_rwlock_unlock(pthread_rwlock_t* rwlock);

int pthread_key_create(pthread_key_t* key, void (*destructor)(void*));
int pthread_key_delete(pthread_key_t key);
void* pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void* value);

int pthread_detach(pthread_t thread);
int pthread_setcancelstate(int state, int* oldstate);
int pthread_setcanceltype(int type, int* oldtype);

int sched_yield(void);

#ifdef __cplusplus
}
#endif

#endif // _PTHREAD_H
