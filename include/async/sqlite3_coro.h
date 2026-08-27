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
    void*                fiber_handle;   /**< Win32 Fiber handle for this coroutine. */
    void*                caller_fiber;   /**< Caller's Win32 Fiber handle for returning on yield. */
    sqlite3_coro_entry_t entry_fn;       /**< User entry point function. */
    void*                arg;            /**< User argument pointer passed to entry_fn. */
    void*                yield_value;    /**< Data pointer passed across yield/resume boundaries. */
    int                  is_done;        /**< 1 if coroutine entry function returned, 0 otherwise. */
    int                  is_running;     /**< 1 if coroutine is currently active/resumed, 0 otherwise. */
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
    if (st && st->entry_fn) {
        st->entry_fn(st->arg);
    }
    if (st) {
        st->is_done = 1;
        st->is_running = 0;
        g_active_coro_state = NULL;
        if (st->caller_fiber) {
            SwitchToFiber(st->caller_fiber);
        }
    }
}
/** @endcond */

/**
 * @brief Initializes and creates a stackful coroutine (fiber).
 *
 * @param coro Pointer to uninitialized sqlite3_coro_t handle.
 * @param stack_size Requested stack size in bytes (pass 0 for default 64KB).
 * @param fn Entry point function to execute inside the coroutine.
 * @param arg Arbitrary user argument passed to `fn`.
 * @return SQLITE_OK on success, SQLITE_NOMEM on allocation failure, or SQLITE_MISUSE on invalid arguments.
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
    st->entry_fn = fn;
    st->arg = arg;
    st->yield_value = NULL;
    st->is_done = 0;
    st->is_running = 0;

    st->fiber_handle = CreateFiber(stack_size, sqlite3_coro_win_trampoline, st);
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
 * @param coro Pointer to the coroutine to resume.
 * @return SQLITE_OK on success, or SQLITE_MISUSE if the coroutine is already finished or invalid.
 */
static inline int sqlite3_coro_resume(sqlite3_coro_t* coro) {
    if (!coro || !coro->state || coro->state->is_done || !coro->state->fiber_handle) {
        return SQLITE_MISUSE;
    }

    sqlite3_coro_state_t* st = coro->state;

    /* Ensure current thread is converted to a fiber so we can switch back */
    void* cur_fiber = GetCurrentFiber();
    if (!cur_fiber || cur_fiber == (void*)0x1e00 /* default invalid Win32 fiber */) {
        cur_fiber = ConvertThreadToFiber(NULL);
        if (!cur_fiber) {
            cur_fiber = GetCurrentFiber();
        }
    }

    st->caller_fiber = cur_fiber;
    st->is_running = 1;

    sqlite3_coro_state_t* prev = g_active_coro_state;
    g_active_coro_state = st;

    SwitchToFiber(st->fiber_handle);

    g_active_coro_state = prev;
    return SQLITE_OK;
}

/**
 * @brief Yields execution back to the caller from inside the active coroutine.
 */
static inline void sqlite3_coro_yield(void) {
    sqlite3_coro_state_t* st = g_active_coro_state;
    if (!st || !st->caller_fiber) return;

    st->is_running = 0;
    SwitchToFiber(st->caller_fiber);
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
 * @brief Retrieves the last value yielded by the coroutine.
 *
 * @param coro Pointer to the coroutine handle.
 * @return Pointer yielded via sqlite3_coro_yield_value, or NULL if none.
 */
static inline void* sqlite3_coro_get_value(const sqlite3_coro_t* coro) {
    return (coro && coro->state) ? coro->state->yield_value : NULL;
}

/**
 * @brief Checks whether the coroutine has completed execution.
 *
 * @param coro Pointer to the coroutine handle.
 * @return Non-zero (1) if finished or invalid, 0 if still active.
 */
static inline int sqlite3_coro_is_done(const sqlite3_coro_t* coro) {
    return (!coro || !coro->state) ? 1 : coro->state->is_done;
}

/**
 * @brief Destroys the coroutine and releases its fiber context and memory.
 *
 * @param coro Pointer to the coroutine handle to destroy.
 */
static inline void sqlite3_coro_destroy(sqlite3_coro_t* coro) {
    if (coro && coro->state) {
        sqlite3_coro_state_t* st = coro->state;
        if (st->fiber_handle) {
            DeleteFiber(st->fiber_handle);
            st->fiber_handle = NULL;
        }
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
    if (st && st->entry_fn) {
        st->entry_fn(st->arg);
    }
    if (st) {
        st->is_done = 1;
        st->is_running = 0;
        g_active_coro_state = NULL;
        setcontext(&st->caller_ctx);
    }
}
/** @endcond */

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

static inline int sqlite3_coro_resume(sqlite3_coro_t* coro) {
    if (!coro || !coro->state || coro->state->is_done || !coro->state->stack_mem) {
        return SQLITE_MISUSE;
    }

    sqlite3_coro_state_t* st = coro->state;
    st->is_running = 1;

    sqlite3_coro_state_t* prev = g_active_coro_state;
    g_active_coro_state = st;

    swapcontext(&st->caller_ctx, &st->ctx);

    g_active_coro_state = prev;
    return SQLITE_OK;
}

static inline void sqlite3_coro_yield(void) {
    sqlite3_coro_state_t* st = g_active_coro_state;
    if (!st) return;

    st->is_running = 0;
    swapcontext(&st->ctx, &st->caller_ctx);
}

static inline void sqlite3_coro_yield_value(void* val) {
    sqlite3_coro_state_t* st = g_active_coro_state;
    if (st) {
        st->yield_value = val;
    }
    sqlite3_coro_yield();
}

static inline void* sqlite3_coro_get_value(const sqlite3_coro_t* coro) {
    return (coro && coro->state) ? coro->state->yield_value : NULL;
}

static inline int sqlite3_coro_is_done(const sqlite3_coro_t* coro) {
    return (!coro || !coro->state) ? 1 : coro->state->is_done;
}

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
