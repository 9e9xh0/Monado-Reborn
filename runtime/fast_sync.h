// [XCDE] Lightweight mutex + condition variable: native SRWLOCK + CONDITION_VARIABLE on Windows
// (avoids libwinpthread's kernel Event/Semaphore + WaitForMultipleObjects overhead on hot paths),
// standard pthread_mutex_t / pthread_cond_t elsewhere.
#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

typedef SRWLOCK FastMutex;
typedef CONDITION_VARIABLE FastCond;
#define FAST_MUTEX_INIT SRWLOCK_INIT
#define FAST_COND_INIT CONDITION_VARIABLE_INIT

static inline void fast_mutex_lock(FastMutex *m) { AcquireSRWLockExclusive(m); }
static inline void fast_mutex_unlock(FastMutex *m) { ReleaseSRWLockExclusive(m); }
static inline void fast_cond_wait(FastCond *c, FastMutex *m) { SleepConditionVariableSRW(c, m, INFINITE, 0); }
static inline void fast_cond_signal(FastCond *c) { WakeConditionVariable(c); }
static inline void fast_cond_broadcast(FastCond *c) { WakeAllConditionVariable(c); }
#else
#include <pthread.h>

typedef pthread_mutex_t FastMutex;
typedef pthread_cond_t FastCond;
#define FAST_MUTEX_INIT PTHREAD_MUTEX_INITIALIZER
#define FAST_COND_INIT PTHREAD_COND_INITIALIZER

static inline void fast_mutex_lock(FastMutex *m) { pthread_mutex_lock(m); }
static inline void fast_mutex_unlock(FastMutex *m) { pthread_mutex_unlock(m); }
static inline void fast_cond_wait(FastCond *c, FastMutex *m) { pthread_cond_wait(c, m); }
static inline void fast_cond_signal(FastCond *c) { pthread_cond_signal(c); }
static inline void fast_cond_broadcast(FastCond *c) { pthread_cond_broadcast(c); }
#endif
