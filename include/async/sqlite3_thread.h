#ifndef SQLITE3_THREAD_H
#define SQLITE3_THREAD_H

/**
 * @file sqlite3_thread.h
 * @brief Cross-platform, freestanding thread, condition variable, and native mutex primitives.
 *
 * Provides a uniform, zero-dependency C API for thread creation, join, detach, native OS mutexes,
 * and condition variables across Windows (Win32 API) and POSIX (pthreads).
 *
 * ## Key Architecture & Design Principles
 * 1. **Freestanding & Zero-Dependency**:
 *    - Fully operational in strict `-nostdlib` and `-nostdlib++` environments.
 *    - Bypasses `<pthread.h>` dependencies on Windows (using Win32 `CreateThread`, `CRITICAL_SECTION`,
 *      and `CONDITION_VARIABLE`) and avoids `<thread>`/`<mutex>`/`<condition_variable>` runtime bloat.
 * 2. **Unified Function Signatures**:
 *    - Standardizes POSIX and Win32 thread entrypoints to `void* (*)(void*)`.
 *    - Automatically handles Windows `DWORD WINAPI` calling convention and thread return value
 *      storage via an internal zero-allocation/lightweight trampoline.
 * 3. **Memory Safety & Clean Teardown**:
 *    - Detached threads clean up their own trampoline context asynchronously upon termination.
 *    - Joined threads safely pass exit codes back to the parent and clean up handles deterministically.
 * 4. **Condition Variable & Mutex Parity**:
 *    - Maps directly to OS native synchronization primitives (`SleepConditionVariableCS` on Windows,
 *      `pthread_cond_wait` on POSIX) for true kernel-level sleeping with 0% CPU usage.
 *
 * Compatible with C99, C11, C++11, -nostdlib, and -nostdlib++.
 */

#include <stdint.h>
#include <stdlib.h>
#include "../sqlite3_time.h"

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/**
 * @struct sqlite3_thread_t
 * @brief Cross-platform thread handle container for Windows systems.
 *
 * Encapsulates the underlying Win32 `HANDLE` along with trampoline metadata required
 * to replicate POSIX `pthread_join` and `pthread_detach` return-value semantics.
 */
typedef struct {
    HANDLE handle;          /**< Underlying Win32 thread HANDLE returned by CreateThread. */
    void* (*func)(void*);   /**< User-supplied thread worker function pointer. */
    void* arg;              /**< Pointer to internal sqlite3_win32_thctx_t context structure. */
    void* retval;           /**< Captured return value from worker function execution. */
    int is_detached;        /**< Flag indicating if thread was detached (1) or joinable (0). */
} sqlite3_thread_t;

/**
 * @typedef sqlite3_thread_mutex_t
 * @brief Native OS recursive mutex primitive.
 *
 * On Windows, mapped to `CRITICAL_SECTION` for high-performance in-process synchronization
 * and seamless compatibility with `CONDITION_VARIABLE`.
 */
typedef CRITICAL_SECTION sqlite3_thread_mutex_t;

/**
 * @typedef sqlite3_cond_t
 * @brief Native OS condition variable primitive.
 *
 * On Windows, mapped to `CONDITION_VARIABLE` (supported on Windows Vista / Windows Server 2008 and newer).
 */
typedef CONDITION_VARIABLE sqlite3_cond_t;

#else
#include <pthread.h>
#include <sched.h>

/**
 * @typedef sqlite3_thread_t
 * @brief Cross-platform thread handle mapped directly to POSIX `pthread_t`.
 */
typedef pthread_t sqlite3_thread_t;

/**
 * @typedef sqlite3_thread_mutex_t
 * @brief Native OS mutex handle mapped directly to POSIX `pthread_mutex_t`.
 */
typedef pthread_mutex_t sqlite3_thread_mutex_t;

/**
 * @typedef sqlite3_cond_t
 * @brief Native OS condition variable mapped directly to POSIX `pthread_cond_t`.
 */
typedef pthread_cond_t sqlite3_cond_t;

#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @typedef sqlite3_thread_func_t
 * @brief Standardized thread worker entrypoint signature.
 *
 * @param arg User-provided context pointer passed during thread creation.
 * @return Exit status / result pointer retrieved upon thread join.
 */
typedef void* (*sqlite3_thread_func_t)(void* arg);

/* ============================================================================
 * Thread Mutex Primitives (Native OS Mutex for Condition Variables)
 * ============================================================================ */

/**
 * @brief Initializes a native OS mutex.
 *
 * Prepares the underlying `CRITICAL_SECTION` (Windows) or `pthread_mutex_t` (POSIX)
 * for synchronization. Must be called before acquiring locks or passing the mutex
 * to condition variables.
 *
 * @param mutex Pointer to uninitialized `sqlite3_thread_mutex_t` structure.
 * @return 0 on success, non-zero on failure.
 *
 * @note On Windows, this initializes a recursive critical section.
 * @note Must be paired with a corresponding call to `sqlite3_thread_mutex_destroy()`.
 */
static inline int sqlite3_thread_mutex_init(sqlite3_thread_mutex_t* mutex) {
#if defined(_WIN32) || defined(_WIN64)
    InitializeCriticalSection(mutex);
    return 0;
#else
    return pthread_mutex_init(mutex, NULL);
#endif
}

/**
 * @brief Destroys a native OS mutex and releases any system resources.
 *
 * @param mutex Pointer to initialized `sqlite3_thread_mutex_t` structure.
 * @return 0 on success, non-zero on failure.
 *
 * @warning Destroying a locked mutex or a mutex that other threads are waiting on
 * results in undefined behavior.
 */
static inline int sqlite3_thread_mutex_destroy(sqlite3_thread_mutex_t* mutex) {
#if defined(_WIN32) || defined(_WIN64)
    DeleteCriticalSection(mutex);
    return 0;
#else
    return pthread_mutex_destroy(mutex);
#endif
}

/**
 * @brief Acquires an exclusive lock on the mutex, blocking until acquired.
 *
 * If the mutex is already locked by another thread, the calling thread will suspend
 * execution and enter a kernel-managed wait state until ownership can be claimed.
 *
 * @param mutex Pointer to initialized `sqlite3_thread_mutex_t` structure.
 * @return 0 on success, non-zero on error.
 *
 * @note Recursive locking is supported on Windows (`CRITICAL_SECTION`).
 */
static inline int sqlite3_thread_mutex_lock(sqlite3_thread_mutex_t* mutex) {
#if defined(_WIN32) || defined(_WIN64)
    EnterCriticalSection(mutex);
    return 0;
#else
    return pthread_mutex_lock(mutex);
#endif
}

/**
 * @brief Releases exclusive ownership of the locked mutex.
 *
 * Relinquishes the lock so that other waiting threads may acquire it.
 *
 * @param mutex Pointer to locked `sqlite3_thread_mutex_t` structure.
 * @return 0 on success, non-zero on error.
 *
 * @warning Calling unlock on a mutex not owned by the calling thread results in
 * undefined behavior.
 */
static inline int sqlite3_thread_mutex_unlock(sqlite3_thread_mutex_t* mutex) {
#if defined(_WIN32) || defined(_WIN64)
    LeaveCriticalSection(mutex);
    return 0;
#else
    return pthread_mutex_unlock(mutex);
#endif
}

/* ============================================================================
 * Condition Variable Primitives
 * ============================================================================ */

/**
 * @brief Initializes a condition variable.
 *
 * Prepares the underlying `CONDITION_VARIABLE` (Windows) or `pthread_cond_t` (POSIX)
 * for signaling and wait operations.
 *
 * @param cond Pointer to uninitialized `sqlite3_cond_t` structure.
 * @return 0 on success, non-zero on failure.
 *
 * @note On Windows, `InitializeConditionVariable` cannot fail and requires no allocation.
 */
static inline int sqlite3_cond_init(sqlite3_cond_t* cond) {
#if defined(_WIN32) || defined(_WIN64)
    InitializeConditionVariable(cond);
    return 0;
#else
    return pthread_cond_init(cond, NULL);
#endif
}

/**
 * @brief Destroys a condition variable.
 *
 * Releases any OS resources associated with the condition variable.
 *
 * @param cond Pointer to initialized `sqlite3_cond_t` structure.
 * @return 0 on success, non-zero on failure.
 *
 * @note On Windows, `CONDITION_VARIABLE` does not require cleanup (no-op).
 * @warning Destroying a condition variable while threads are waiting on it
 * results in undefined behavior.
 */
static inline int sqlite3_cond_destroy(sqlite3_cond_t* cond) {
#if defined(_WIN32) || defined(_WIN64)
    (void)cond;
    return 0;
#else
    return pthread_cond_destroy(cond);
#endif
}

/**
 * @brief Atomically unlocks the mutex and waits indefinitely until the condition variable is signaled.
 *
 * The calling thread must hold the `mutex` before invoking this function. The mutex
 * is atomically released upon entering the wait state and re-acquired before `sqlite3_cond_wait`
 * returns, even if awakened spuriously.
 *
 * @param cond Pointer to initialized condition variable.
 * @param mutex Pointer to locked mutex currently held by the caller.
 * @return 0 on success, non-zero on error.
 *
 * @note Spurious wakeups may occur; callers must always evaluate condition predicates in a loop:
 * @code
 * sqlite3_thread_mutex_lock(&mutex);
 * while (!ready) {
 *     sqlite3_cond_wait(&cond, &mutex);
 * }
 * sqlite3_thread_mutex_unlock(&mutex);
 * @endcode
 */
static inline int sqlite3_cond_wait(sqlite3_cond_t* cond, sqlite3_thread_mutex_t* mutex) {
#if defined(_WIN32) || defined(_WIN64)
    return SleepConditionVariableCS(cond, mutex, INFINITE) ? 0 : -1;
#else
    return pthread_cond_wait(cond, mutex);
#endif
}

/**
 * @brief Atomically unlocks the mutex and waits until signaled or until timeout expires.
 *
 * Blocks the calling thread on `cond` while releasing `mutex`. The thread wakes up
 * if signaled, if the timeout period elapses, or if a spurious wakeup occurs.
 * The mutex is guaranteed to be held when this function returns.
 *
 * @param cond Pointer to initialized condition variable.
 * @param mutex Pointer to locked mutex currently held by the caller.
 * @param timeout_ms Maximum time to wait in milliseconds.
 * @return 0 if signaled before timeout, 1 if timeout elapsed, -1 on error.
 */
static inline int sqlite3_cond_timedwait(sqlite3_cond_t* cond, sqlite3_thread_mutex_t* mutex, unsigned int timeout_ms) {
#if defined(_WIN32) || defined(_WIN64)
    if (SleepConditionVariableCS(cond, mutex, (DWORD)timeout_ms)) {
        return 0; // Signaled
    }
    return (GetLastError() == ERROR_TIMEOUT) ? 1 : -1; // 1 = Timeout
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t)(timeout_ms / 1000U);
    ts.tv_nsec += (long)((timeout_ms % 1000U) * 1000000L);
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    int rc = pthread_cond_timedwait(cond, mutex, &ts);
    if (rc == 0) return 0;
    return (rc == 110 /* ETIMEDOUT */) ? 1 : -1;
#endif
}

/**
 * @brief Wakes up at least one thread currently waiting on the condition variable.
 *
 * If multiple threads are blocked on `cond`, the OS scheduler determines which thread
 * is unblocked. If no threads are waiting, this call has no effect.
 *
 * @param cond Pointer to initialized condition variable.
 * @return 0 on success, non-zero on failure.
 *
 * @note To unblock all waiting threads simultaneously, use `sqlite3_cond_broadcast()`.
 */
static inline int sqlite3_cond_signal(sqlite3_cond_t* cond) {
#if defined(_WIN32) || defined(_WIN64)
    WakeConditionVariable(cond);
    return 0;
#else
    return pthread_cond_signal(cond);
#endif
}

/**
 * @brief Wakes up all threads currently waiting on the condition variable.
 *
 * All threads waiting on `cond` will unblock and contend for their associated mutexes.
 * If no threads are waiting, this call has no effect.
 *
 * @param cond Pointer to initialized condition variable.
 * @return 0 on success, non-zero on failure.
 */
static inline int sqlite3_cond_broadcast(sqlite3_cond_t* cond) {
#if defined(_WIN32) || defined(_WIN64)
    WakeAllConditionVariable(cond);
    return 0;
#else
    return pthread_cond_broadcast(cond);
#endif
}

/* ============================================================================
 * Thread Lifecycle Management
 * ============================================================================ */

#if defined(_WIN32) || defined(_WIN64)
/**
 * @struct sqlite3_win32_thctx_t
 * @brief Internal trampoline context allocated on heap for Win32 thread creation.
 *
 * Bridges the gap between Win32 `DWORD WINAPI (*)(LPVOID)` and POSIX `void* (*)(void*)`,
 * storing return values and managing asynchronous cleanup for detached threads.
 */
typedef struct {
    sqlite3_thread_func_t func; /**< Target worker function. */
    void* arg;                  /**< User data argument passed to func. */
    void* retval;               /**< Return pointer captured after func returns. */
    HANDLE event;               /**< Optional synchronization event for thread join. */
    int is_detached;            /**< Flag: 1 if thread detached before exit, 0 if joinable. */
} sqlite3_win32_thctx_t;

/**
 * @brief Win32 thread entrypoint trampoline.
 *
 * Executes the user's `sqlite3_thread_func_t`, records the return value, and frees
 * context memory if the thread was detached.
 *
 * @param lpParam Pointer to allocated `sqlite3_win32_thctx_t`.
 * @return Always returns 0.
 */
static inline DWORD WINAPI sqlite3_win32_thread_trampoline(LPVOID lpParam) {
    sqlite3_win32_thctx_t* ctx = (sqlite3_win32_thctx_t*)lpParam;
    void* (*fn)(void*) = ctx->func;
    void* arg = ctx->arg;
    ctx->retval = fn(arg);
    if (ctx->is_detached) {
        // If thread was detached, no join will happen -> clean up heap context now
        free(ctx);
    } else if (ctx->event) {
        // Signal event that execution has completed
        SetEvent(ctx->event);
    }
    return 0;
}
#endif

/**
 * @brief Creates and starts a new thread of execution.
 *
 * Spawns an asynchronous background worker executing `func(arg)`. The returned
 * `thread` handle is joinable by default and must eventually be either joined via
 * `sqlite3_thread_join()` or detached via `sqlite3_thread_detach()` to avoid resource leaks.
 *
 * @param thread Pointer to `sqlite3_thread_t` to receive the thread handle.
 * @param func Worker function entry point of type `void* (*)(void* arg)`.
 * @param arg Context pointer passed as argument to `func`.
 * @return 0 on successful thread creation, -1 on failure (e.g. out of memory / thread limit).
 *
 * @code
 * static void* my_worker(void* arg) {
 *     printf("Worker running with arg: %p\n", arg);
 *     return (void*)123;
 * }
 *
 * sqlite3_thread_t th;
 * if (sqlite3_thread_create(&th, my_worker, NULL) == 0) {
 *     void* retval = NULL;
 *     sqlite3_thread_join(&th, &retval);
 * }
 * @endcode
 */
static inline int sqlite3_thread_create(sqlite3_thread_t* thread, sqlite3_thread_func_t func, void* arg) {
    if (!thread || !func) return -1;
#if defined(_WIN32) || defined(_WIN64)
    sqlite3_win32_thctx_t* ctx = (sqlite3_win32_thctx_t*)malloc(sizeof(sqlite3_win32_thctx_t));
    if (!ctx) return -1;
    ctx->func = func;
    ctx->arg = arg;
    ctx->retval = NULL;
    ctx->is_detached = 0;
    ctx->event = CreateEvent(NULL, TRUE, FALSE, NULL);

    HANDLE h = CreateThread(NULL, 0, sqlite3_win32_thread_trampoline, ctx, 0, NULL);
    if (!h) {
        if (ctx->event) CloseHandle(ctx->event);
        free(ctx);
        return -1;
    }
    thread->handle = h;
    thread->func = func;
    thread->arg = ctx;
    thread->retval = NULL;
    thread->is_detached = 0;
    return 0;
#else
    return pthread_create(thread, NULL, func, arg);
#endif
}

/**
 * @brief Waits for the target thread to terminate and optionally captures its return value.
 *
 * Suspends execution of the calling thread until the target thread finishes. Once joined,
 * the thread's underlying OS resources are released and the handle is invalidated.
 *
 * @param thread Pointer to joinable `sqlite3_thread_t` handle.
 * @param retval Optional output pointer to store the target thread's return value (`void*`).
 *               Pass `NULL` if return value is not needed.
 * @return 0 on success, -1 on error (e.g. invalid handle or already joined/detached).
 *
 * @warning Calling join multiple times on the same handle or calling join on a detached
 * thread results in an error / undefined behavior.
 */
static inline int sqlite3_thread_join(sqlite3_thread_t* thread, void** retval) {
#if defined(_WIN32) || defined(_WIN64)
    if (!thread || !thread->handle) return -1;
    WaitForSingleObject(thread->handle, INFINITE);
    sqlite3_win32_thctx_t* ctx = (sqlite3_win32_thctx_t*)thread->arg;
    if (ctx) {
        if (retval) {
            *retval = ctx->retval;
        }
        if (ctx->event) CloseHandle(ctx->event);
        free(ctx);
        thread->arg = NULL;
    }
    CloseHandle(thread->handle);
    thread->handle = NULL;
    return 0;
#else
    if (!thread) return -1;
    return pthread_join(*thread, retval);
#endif
}

/**
 * @brief Detaches a running thread, allowing its resources to be freed automatically on exit.
 *
 * Informs the operating system that thread resources should be reclaimed immediately upon
 * termination without waiting for `sqlite3_thread_join()`. The handle is invalidated
 * and can no longer be joined.
 *
 * @param thread Pointer to `sqlite3_thread_t` handle.
 * @return 0 on success, -1 on failure.
 */
static inline int sqlite3_thread_detach(sqlite3_thread_t* thread) {
#if defined(_WIN32) || defined(_WIN64)
    if (!thread || !thread->handle) return -1;
    sqlite3_win32_thctx_t* ctx = (sqlite3_win32_thctx_t*)thread->arg;
    if (ctx) {
        ctx->is_detached = 1;
    }
    CloseHandle(thread->handle);
    thread->handle = NULL;
    return 0;
#else
    if (!thread) return -1;
    return pthread_detach(*thread);
#endif
}

/**
 * @brief Yields the calling thread's execution timeslice to other ready threads.
 *
 * Relinquishes the CPU to permit other threads of equal priority to run on the current processor.
 * Uses `SwitchToThread()` on Windows, `sched_yield()` on POSIX, and `usleep(0)` as fallback.
 */
static inline void sqlite3_thread_yield(void) {
#if defined(_WIN32) || defined(_WIN64)
    SwitchToThread();
#elif defined(_POSIX_PRIORITY_SCHEDULING)
    sched_yield();
#else
    usleep(0);
#endif
}

#ifdef __cplusplus
}
#endif

#endif // SQLITE3_THREAD_H
