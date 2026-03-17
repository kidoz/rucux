// SPDX-License-Identifier: MIT
#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t pthread_t;
typedef uint32_t pthread_mutex_t;
typedef void pthread_attr_t;
typedef void pthread_mutexattr_t;
typedef int pthread_once_t;

#define PTHREAD_MUTEX_INITIALIZER 0
#define PTHREAD_ONCE_INIT 0

int pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start_routine)(void*), void* arg);

int pthread_join(pthread_t thread, void** retval);

int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr);
int pthread_mutex_destroy(pthread_mutex_t* mutex);
int pthread_mutex_lock(pthread_mutex_t* mutex);
int pthread_mutex_unlock(pthread_mutex_t* mutex);

pthread_t pthread_self(void);
int pthread_equal(pthread_t t1, pthread_t t2);
int pthread_once(pthread_once_t* once_control, void (*init_routine)(void));

int sched_yield(void);

#ifdef __cplusplus
}
#endif

#endif // _PTHREAD_H
