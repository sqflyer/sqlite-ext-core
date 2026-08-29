#ifndef SQLITE3_CORO_H
#define SQLITE3_CORO_H

/**
 * @file sqlite3_coro.h
 * @brief Zero-dependency, freestanding Pure C Stackful Coroutine & Fiber Subsystem.
 *
 * Provides cooperative multitasking, stackful context switching, and value yielding
 * without standard library runtime dependencies. Uses native Win32 Fibers on Windows
 * and POSIX ucontext_t on POSIX platforms, with stack and state memory allocated
 * strictly via sqlite3_malloc64 / sqlite3_free.
 */

#include <sqlite3.h>
#include <stddef.h>
#include <stdint.h>

/*
 * AddressSanitizer (ASAN) Fiber Annotations:
 * On POSIX (ucontext_t), custom user-allocated stacks require explicit ASAN fiber
 * switch notifications (__sanitizer_start_switch_fiber / __sanitizer_finish_switch_fiber).
 *
 * On Windows (_WIN32 / _WIN64), Win32 Fibers (CreateFiber / SwitchToFiber) are native OS
 * objects whose stack bounds and TEB (Thread Environment Block) registers are managed
 * directly by the Windows NT kernel. The LLVM ASAN runtime on Windows does not support
 * cross-thread fiber migrations (M:N scheduling where a fiber yields on Thread A and resumes
 * on Thread B). Therefore, ASAN fiber hooks are strictly enabled for POSIX ucontext targets.
 */
#if defined(__SANITIZE_ADDRESS__)
    #if !defined(_WIN32) && !defined(_WIN64)
        #define SQLITE3_CORO_ASAN 1
    #endif
#elif defined(__has_feature)
    #if __has_feature(address_sanitizer) && !defined(_WIN32) && !defined(_WIN64)
        #define SQLITE3_CORO_ASAN 1
    #endif
#endif

#if defined(SQLITE3_CORO_ASAN)
    #ifdef __cplusplus
    extern "C" {
    #endif
        void __sanitizer_start_switch_fiber(void** fake_stack_save, const void* stack_bottom, size_t stack_size);
        void __sanitizer_finish_switch_fiber(void* fake_stack_save, const void** old_stack_bottom, size_t* old_stack_size);
    #ifdef __cplusplus
    }
    #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Default stack size for newly created coroutines (64 KB).
 */
#define SQLITE3_CORO_DEFAULT_STACK_SIZE (64 * 1024)

/**
 * @brief Function pointer signature for coroutine entry routines.
 * @param arg User-defined pointer passed at creation time.
 */
typedef void (*sqlite3_coro_entry_t)(void* arg);

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/**
 * @struct sqlite3_coro_state
 * @brief Internal heap-allocated state holding fiber handles and execution flags.
 */
typedef struct sqlite3_coro_state {
    void*                fiber_handle;      /**< Win32 Fiber handle for this coroutine. */
    void*                caller_fiber;      /**< Caller's Win32 Fiber handle for returning on yield. */
    size_t               stack_size;        /**< Requested stack size in bytes. */
    sqlite3_coro_entry_t entry_fn;          /**< User entry point function. */
    void*                arg;               /**< User argument pointer passed to entry_fn. */
    void*                yield_value;       /**< Data pointer passed across yield/resume boundaries. */
    int                  is_done;           /**< 1 if coroutine entry function returned, 0 otherwise. */
    int                  is_running;        /**< 1 if coroutine is currently active/resumed, 0 otherwise. */
} sqlite3_coro_state_t;

/**
 * @struct sqlite3_coro
 * @brief Coroutine handle struct.
 */
typedef struct sqlite3_coro {
    sqlite3_coro_state_t* state;         /**< Stable heap state pointer. */
} sqlite3_coro_t;

#if defined(_MSC_VER)
#define SQLITE_CORO_THREAD_LOCAL __declspec(thread)
#else
#define SQLITE_CORO_THREAD_LOCAL __thread
#endif

/** @cond INTERNAL */
static SQLITE_CORO_THREAD_LOCAL sqlite3_coro_state_t* g_active_coro_state = NULL;

/**
 * @brief Internal Win32 fiber entry trampoline.
 * @param param Pointer to the stable sqlite3_coro_state_t.
 */
static void CALLBACK sqlite3_coro_win_trampoline(void* param) {
    sqlite3_coro_state_t* st = (sqlite3_coro_state_t*)param;
#if defined(SQLITE3_CORO_ASAN)
    __sanitizer_finish_switch_fiber(NULL, NULL, NULL);
#endif
    if (st && st->entry_fn) {
        st->entry_fn(st->arg);
    }
    if (st) {
        void* caller = st->caller_fiber;
        st->is_done = 1;
        st->is_running = 0;
        g_active_coro_state = NULL;
        while (caller) {
#if defined(SQLITE3_CORO_ASAN)
            __sanitizer_start_switch_fiber(NULL, NULL, 0);
#endif
            SwitchToFiber(caller);
        }
    }
}
/** @endcond */

#if defined(_WIN32) || defined(_WIN64)
#ifndef FIBER_FLAG_FLOAT_SWITCH
#define FIBER_FLAG_FLOAT_SWITCH 0x1
#endif
#endif

/**
 * @brief Initializes and creates a stackful coroutine (fiber).
 *
 * Allocates an internal state structure and dedicated fiber context. On Windows,
 * creates a Win32 Fiber via `CreateFiberEx`. Memory is allocated strictly via `sqlite3_malloc64`.
 *
 * @param coro Pointer to uninitialized `sqlite3_coro_t` handle.
 * @param stack_size Requested fiber stack size in bytes (pass 0 for default 64KB).
 * @param fn Entry point function to execute inside the coroutine.
 * @param arg Arbitrary user argument passed to `fn`.
 * @return `SQLITE_OK` on success, `SQLITE_NOMEM` on allocation failure, or `SQLITE_MISUSE` on invalid arguments.
 */
static inline int sqlite3_coro_create(
    sqlite3_coro_t* coro,
    size_t stack_size,
    sqlite3_coro_entry_t fn,
    void* arg)
{
    if (!coro || !fn) return SQLITE_MISUSE;
    if (stack_size == 0) stack_size = SQLITE3_CORO_DEFAULT_STACK_SIZE;

    sqlite3_coro_state_t* st = (sqlite3_coro_state_t*)sqlite3_malloc64(sizeof(sqlite3_coro_state_t));
    if (!st) return SQLITE_NOMEM;

    st->caller_fiber = NULL;
    st->stack_size = stack_size;
    st->entry_fn = fn;
    st->arg = arg;
    st->yield_value = NULL;
    st->is_done = 0;
    st->is_running = 0;

#if defined(_WIN32) || defined(_WIN64)
    st->fiber_handle = CreateFiberEx(stack_size, stack_size, FIBER_FLAG_FLOAT_SWITCH, sqlite3_coro_win_trampoline, st);
    if (!st->fiber_handle) {
        st->fiber_handle = CreateFiber(stack_size, sqlite3_coro_win_trampoline, st);
    }
#endif

    if (!st->fiber_handle) {
        sqlite3_free(st);
        return SQLITE_NOMEM;
    }

    coro->state = st;
    return SQLITE_OK;
}

/**
 * @brief Resumes execution of the coroutine from its last yield point.
 *
 * Converts the calling OS thread to a fiber if necessary, saves the active caller
 * context, and performs a stackful hardware context switch to the coroutine fiber.
 *
 * @param coro Pointer to the coroutine to resume.
 * @return `SQLITE_OK` on success, or `SQLITE_MISUSE` if the coroutine is finished, running, or invalid.
 */
static inline int sqlite3_coro_resume(sqlite3_coro_t* coro) {
    if (!coro || !coro->state || coro->state->is_done || !coro->state->fiber_handle) {
        return SQLITE_MISUSE;
    }

    sqlite3_coro_state_t* st = coro->state;

    /* Ensure current thread is converted to a fiber with SSE/Float preservation */
    void* cur_fiber = GetCurrentFiber();
    if (!cur_fiber || (uintptr_t)cur_fiber < (uintptr_t)0x10000) {
        cur_fiber = ConvertThreadToFiberEx(NULL, FIBER_FLAG_FLOAT_SWITCH);
        if (!cur_fiber) {
            cur_fiber = ConvertThreadToFiber(NULL);
        }
        cur_fiber = GetCurrentFiber();
    }

    if (!cur_fiber || (uintptr_t)cur_fiber < (uintptr_t)0x10000) {
        return SQLITE_ERROR;
    }

    st->caller_fiber = cur_fiber;
    st->is_running = 1;

    sqlite3_coro_state_t* prev = g_active_coro_state;
    g_active_coro_state = st;

#if defined(SQLITE3_CORO_ASAN)
    __sanitizer_start_switch_fiber(NULL, NULL, 0);
#endif

    SwitchToFiber(st->fiber_handle);

#if defined(SQLITE3_CORO_ASAN)
    __sanitizer_finish_switch_fiber(NULL, NULL, NULL);
#endif

    g_active_coro_state = prev;
    return SQLITE_OK;
}

/**
 * @brief Retrieves the active coroutine state from the current fiber context.
 *
 * Uses native `GetFiberData()` on Windows to guarantee exact fiber state resolution
 * even when fibers migrate across multiple OS worker threads.
 */
static inline sqlite3_coro_state_t* sqlite3_coro_active_state(void) {
#if defined(_WIN32) || defined(_WIN64)
    sqlite3_coro_state_t* st = (sqlite3_coro_state_t*)GetFiberData();
    if (st && (uintptr_t)st >= (uintptr_t)0x10000) return st;
    return g_active_coro_state;
#else
    return g_active_coro_state;
#endif
}

/**
 * @brief Yields execution back to the caller from inside the active coroutine.
 *
 * Suspends the active coroutine's execution, preserves its entire call stack and CPU
 * register state, and transfers control back to the thread or fiber that called `sqlite3_coro_resume`.
 */
static inline void sqlite3_coro_yield(void) {
    sqlite3_coro_state_t* st = sqlite3_coro_active_state();
    if (!st || !st->caller_fiber) return;

    st->is_running = 0;

#if defined(SQLITE3_CORO_ASAN)
    __sanitizer_start_switch_fiber(NULL, NULL, 0);
#endif

    SwitchToFiber(st->caller_fiber);

#if defined(SQLITE3_CORO_ASAN)
    __sanitizer_finish_switch_fiber(NULL, NULL, NULL);
#endif
}

/**
 * @brief Yields execution and transmits a data pointer to the resuming caller.
 *
 * Sets the yielded value pointer in the coroutine state, then suspends execution.
 * The caller can retrieve the value via `sqlite3_coro_get_value`.
 *
 * @param val Pointer to data to yield back to caller.
 */
static inline void sqlite3_coro_yield_value(void* val) {
    sqlite3_coro_state_t* st = sqlite3_coro_active_state();
    if (st) {
        st->yield_value = val;
    }
    sqlite3_coro_yield();
}

/**
 * @brief Retrieves the last value yielded by the coroutine.
 *
 * @param coro Pointer to the coroutine handle.
 * @return Pointer yielded via `sqlite3_coro_yield_value`, or NULL if none.
 */
static inline void* sqlite3_coro_get_value(const sqlite3_coro_t* coro) {
    return (coro && coro->state) ? coro->state->yield_value : NULL;
}

/**
 * @brief Checks whether the coroutine has completed execution.
 *
 * @param coro Pointer to the coroutine handle.
 * @return Non-zero (1) if finished or invalid, 0 if still active and suspendable.
 */
static inline int sqlite3_coro_is_done(const sqlite3_coro_t* coro) {
    return (!coro || !coro->state) ? 1 : coro->state->is_done;
}

/**
 * @brief Destroys the coroutine and releases its fiber context and memory.
 *
 * Frees the underlying Win32 fiber handle and releases the heap state via `sqlite3_free`.
 * Safe to call on suspended or already completed coroutines.
 *
 * @param coro Pointer to the coroutine handle to destroy.
 */
static inline void sqlite3_coro_destroy(sqlite3_coro_t* coro) {
    if (coro && coro->state) {
        sqlite3_coro_state_t* st = coro->state;
#if defined(_WIN32) || defined(_WIN64)
        if (st->fiber_handle) {
            DeleteFiber(st->fiber_handle);
            st->fiber_handle = NULL;
        }
#endif
        sqlite3_free(st);
        coro->state = NULL;
    }
}

#else /* POSIX (Linux / macOS) using ucontext_t */

#include <ucontext.h>

/**
 * @struct sqlite3_coro_state
 * @brief Internal heap-allocated state holding POSIX context and execution flags.
 */
typedef struct sqlite3_coro_state {
    ucontext_t           ctx;            /**< POSIX ucontext for this coroutine. */
    ucontext_t           caller_ctx;     /**< Caller context for returning on yield. */
    void*                stack_mem;      /**< Stack memory buffer allocated via sqlite3_malloc64. */
    size_t               stack_size;     /**< Allocated stack size in bytes. */
    sqlite3_coro_entry_t entry_fn;       /**< User entry function. */
    void*                arg;            /**< User argument passed to entry_fn. */
    void*                yield_value;    /**< Data passed across yield/resume boundaries. */
    int                  is_done;        /**< 1 if done, 0 otherwise. */
    int                  is_running;     /**< 1 if running, 0 otherwise. */
} sqlite3_coro_state_t;

/**
 * @struct sqlite3_coro
 * @brief Coroutine handle struct.
 */
typedef struct sqlite3_coro {
    sqlite3_coro_state_t* state;         /**< Stable heap state pointer. */
} sqlite3_coro_t;

#if defined(_MSC_VER)
#define SQLITE_CORO_THREAD_LOCAL __declspec(thread)
#else
#define SQLITE_CORO_THREAD_LOCAL __thread
#endif

/** @cond INTERNAL */
static SQLITE_CORO_THREAD_LOCAL sqlite3_coro_state_t* g_active_coro_state = NULL;

static void sqlite3_coro_posix_trampoline(uint32_t hi, uint32_t lo) {
    uintptr_t ptr = (((uintptr_t)hi) << 32) | (uintptr_t)lo;
    sqlite3_coro_state_t* st = (sqlite3_coro_state_t*)ptr;
#if defined(SQLITE3_CORO_ASAN)
    __sanitizer_finish_switch_fiber(NULL, NULL, NULL);
#endif
    if (st && st->entry_fn) {
        st->entry_fn(st->arg);
    }
    if (st) {
        st->is_done = 1;
        st->is_running = 0;
        g_active_coro_state = NULL;
#if defined(SQLITE3_CORO_ASAN)
        __sanitizer_start_switch_fiber(NULL, NULL, 0);
#endif
        setcontext(&st->caller_ctx);
    }
}
/** @endcond */

/**
 * @brief Initializes and creates a stackful POSIX coroutine (ucontext).
 *
 * Allocates stack memory via `sqlite3_malloc64` and configures the POSIX `ucontext_t`
 * execution state via `makecontext`.
 *
 * @param coro Pointer to uninitialized `sqlite3_coro_t` handle.
 * @param stack_size Requested stack size in bytes (pass 0 for default 64KB).
 * @param fn Entry point function to execute inside the coroutine.
 * @param arg Arbitrary user argument passed to `fn`.
 * @return `SQLITE_OK` on success, `SQLITE_NOMEM` on allocation failure, or `SQLITE_MISUSE` on invalid arguments.
 */
static inline int sqlite3_coro_create(
    sqlite3_coro_t* coro,
    size_t stack_size,
    sqlite3_coro_entry_t fn,
    void* arg)
{
    if (!coro || !fn) return SQLITE_MISUSE;
    if (stack_size == 0) stack_size = SQLITE3_CORO_DEFAULT_STACK_SIZE;

    sqlite3_coro_state_t* st = (sqlite3_coro_state_t*)sqlite3_malloc64(sizeof(sqlite3_coro_state_t));
    if (!st) return SQLITE_NOMEM;

    st->stack_size = stack_size;
    st->stack_mem = sqlite3_malloc64((sqlite3_uint64)stack_size);
    if (!st->stack_mem) {
        sqlite3_free(st);
        return SQLITE_NOMEM;
    }

    st->entry_fn = fn;
    st->arg = arg;
    st->yield_value = NULL;
    st->is_done = 0;
    st->is_running = 0;

    if (getcontext(&st->ctx) != 0) {
        sqlite3_free(st->stack_mem);
        sqlite3_free(st);
        return SQLITE_ERROR;
    }

    st->ctx.uc_stack.ss_sp = st->stack_mem;
    st->ctx.uc_stack.ss_size = st->stack_size;
    st->ctx.uc_stack.ss_flags = 0;
    st->ctx.uc_link = &st->caller_ctx;

    uintptr_t ptr = (uintptr_t)st;
    uint32_t hi = (uint32_t)(ptr >> 32);
    uint32_t lo = (uint32_t)(ptr & 0xFFFFFFFF);

    makecontext(&st->ctx, (void (*)(void))sqlite3_coro_posix_trampoline, 2, hi, lo);
    coro->state = st;
    return SQLITE_OK;
}

/**
 * @brief Resumes execution of the POSIX coroutine from its last yield point.
 *
 * Saves the caller's context into `caller_ctx` and performs a hardware context switch
 * via `swapcontext`.
 *
 * @param coro Pointer to the coroutine to resume.
 * @return `SQLITE_OK` on success, or `SQLITE_MISUSE` if the coroutine is finished, running, or invalid.
 */
static inline int sqlite3_coro_resume(sqlite3_coro_t* coro) {
    if (!coro || !coro->state || coro->state->is_done || !coro->state->stack_mem) {
        return SQLITE_MISUSE;
    }

    sqlite3_coro_state_t* st = coro->state;
    st->is_running = 1;

    sqlite3_coro_state_t* prev = g_active_coro_state;
    g_active_coro_state = st;

#if defined(SQLITE3_CORO_ASAN)
    __sanitizer_start_switch_fiber(NULL, st->stack_mem, st->stack_size);
#endif

    swapcontext(&st->caller_ctx, &st->ctx);

#if defined(SQLITE3_CORO_ASAN)
    __sanitizer_finish_switch_fiber(NULL, NULL, NULL);
#endif

    g_active_coro_state = prev;
    return SQLITE_OK;
}

/**
 * @brief Yields execution back to the caller from inside the active POSIX coroutine.
 *
 * Saves the coroutine's context and swaps back to the caller context via `swapcontext`.
 */
static inline void sqlite3_coro_yield(void) {
    sqlite3_coro_state_t* st = g_active_coro_state;
    if (!st) return;

    st->is_running = 0;

#if defined(SQLITE3_CORO_ASAN)
    __sanitizer_start_switch_fiber(NULL, NULL, 0);
#endif

    swapcontext(&st->ctx, &st->caller_ctx);

#if defined(SQLITE3_CORO_ASAN)
    __sanitizer_finish_switch_fiber(NULL, NULL, NULL);
#endif
}

/**
 * @brief Yields execution and transmits a data pointer to the resuming caller.
 *
 * @param val Pointer to data to yield back to caller.
 */
static inline void sqlite3_coro_yield_value(void* val) {
    sqlite3_coro_state_t* st = g_active_coro_state;
    if (st) {
        st->yield_value = val;
    }
    sqlite3_coro_yield();
}

/**
 * @brief Retrieves the last value yielded by the POSIX coroutine.
 *
 * @param coro Pointer to the coroutine handle.
 * @return Pointer yielded via `sqlite3_coro_yield_value`, or NULL if none.
 */
static inline void* sqlite3_coro_get_value(const sqlite3_coro_t* coro) {
    return (coro && coro->state) ? coro->state->yield_value : NULL;
}

/**
 * @brief Checks whether the POSIX coroutine has completed execution.
 *
 * @param coro Pointer to the coroutine handle.
 * @return Non-zero (1) if finished or invalid, 0 if still active and suspendable.
 */
static inline int sqlite3_coro_is_done(const sqlite3_coro_t* coro) {
    return (!coro || !coro->state) ? 1 : coro->state->is_done;
}

/**
 * @brief Destroys the POSIX coroutine and frees its stack and heap memory.
 *
 * Releases the stack memory buffer and internal state via `sqlite3_free`.
 *
 * @param coro Pointer to the coroutine handle to destroy.
 */
static inline void sqlite3_coro_destroy(sqlite3_coro_t* coro) {
    if (coro && coro->state) {
        sqlite3_coro_state_t* st = coro->state;
        if (st->stack_mem) {
            sqlite3_free(st->stack_mem);
            st->stack_mem = NULL;
        }
        sqlite3_free(st);
        coro->state = NULL;
    }
}

#endif /* _WIN32 / POSIX */

#ifdef __cplusplus
}
#endif

#endif /* SQLITE3_CORO_H */
