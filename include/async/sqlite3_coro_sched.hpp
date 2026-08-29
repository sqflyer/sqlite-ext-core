#ifndef SQLITE3_CORO_SCHED_HPP
#define SQLITE3_CORO_SCHED_HPP

/**
 * @file sqlite3_coro_sched.hpp
 * @brief Zero-dependency C++11/C++20 M:N Cooperative Coroutine Scheduler & Worker Pool.
 *
 * ## Architecture Overview
 * Modern, high-performance C++ wrapper providing ergonomic task scheduling, lambda closure execution,
 * and process-wide reference-counted lifecycle management for SQLite extensions.
 *
 * ### Key Architectural Features:
 * 1. **M:N Cooperative Coroutine Execution**:
 *    - Maps $M$ stackful coroutines onto $N$ OS worker threads with automatic load balancing.
 *    - Supports synchronous execution on the main calling thread via `poll_one()`, `run_until_empty()`,
 *      and `run_local()` (essential for single-threaded / WebAssembly environments and thread-affine SQLite APIs).
 *
 * 2. **Type-Erased Closure & Functor Dispatch**:
 *    - Seamlessly accepts lambda expressions with captures, move-only functors, and free functions
 *      without standard library overhead (`-nostdlib++` safe, 0% `<functional>` bloat).
 *
 * 3. **Stable Pointer Model & Safe Move Semantics**:
 *    - The underlying `sqlite3_coro_pool_t` structure resides on a stable heap address tracked
 *      via SQLite's allocator. Move operations safely transfer ownership without corrupting worker
 *      thread pointer contexts.
 *
 * 4. **Process-Wide Global Lifecycle Management**:
 *    - Thread-safe singleton acquisition (`acquire_global()` / `release_global()`) with atomic
 *      reference counting, allowing multiple loaded databases in a process to share a single
 *      worker thread pool without redundant OS thread creation.
 *
 * 5. **Standalone Template Spawn Helpers**:
 *    - `sqlite_coro_spawn(pool, callable, stack_size)`: Template helper for concise inline task submission.
 *    - `sqlite_coro_spawn_stack<Size>(pool, callable)`: Template helper with compile-time stack size.
 */

#include "sqlite3_coro_sched.h"
#include "sqlite3_coro.hpp"
#include "sqlite3_thread.hpp"
#include "../sqlite3_atomic.hpp"
#include "../sqlite3_allocator.hpp"

// ============================================================================
// C++ COROUTINE SCHEDULER & WORKER POOL
// ============================================================================

/**
 * @class SqliteCoroScheduler
 * @brief High-performance RAII M:N Cooperative Coroutine Scheduler and Worker Pool.
 *
 * Provides a modern C++11 interface over `sqlite3_coro_sched.h` with zero runtime overhead:
 * - Schedules $M$ cooperative stackful coroutines (fibers) across $N$ OS worker threads.
 * - Supports capturing lambdas, move-only functors, and raw function pointers without `<functional>` bloat.
 * - Supports synchronous main-thread execution (`poll_one`, `run_until_empty`, `run_local`).
 * - Manages process-wide singleton lifetimes via `acquire_global()` / `release_global()`.
 *
 * @code
 * SqliteCoroScheduler sched(4); // 4 background worker threads
 * sched.spawn([]() {
 *     printf("Running in fiber\n");
 *     SqliteCoroScheduler::yield();
 *     printf("Resumed in fiber\n");
 * });
 * sched.wait_all();
 * @endcode
 */
class SqliteCoroScheduler {
private:
    sqlite3_coro_pool_t* m_pool;  /**< Pointer to heap-allocated stable C pool descriptor. */

public:
    /**
     * @struct TaskClosureBase
     * @brief Type-erased base structure for dispatching C++ callable closures on C coroutine trampolines.
     */
    struct TaskClosureBase {
        void (*invoke_fn)(TaskClosureBase*);   /**< Invocation function pointer. */
        void (*destroy_fn)(TaskClosureBase*);  /**< Deallocation function pointer. */
    };

    /**
     * @struct TaskClosure
     * @brief Templated closure container storing user-supplied functors and lambdas.
     * @tparam F Type of the stored callable object.
     */
    template <typename F>
    struct TaskClosure : public TaskClosureBase {
        F func;  /**< Stored callable instance. */

        /** @brief Constructs closure from const lvalue reference. */
        TaskClosure(const F& f) : func(f) {
            invoke_fn = &invoke_impl;
            destroy_fn = &destroy_impl;
        }

        /** @brief Constructs closure from rvalue reference (move semantics). */
        TaskClosure(F&& f) : func(sqlite_move(f)) {
            invoke_fn = &invoke_impl;
            destroy_fn = &destroy_impl;
        }

        /** @brief Static trampoline invoking the stored callable. */
        static void invoke_impl(TaskClosureBase* self) {
            static_cast<TaskClosure<F>*>(self)->func();
        }

        /** @brief Static trampoline freeing the closure container via sqlite_delete. */
        static void destroy_impl(TaskClosureBase* self) {
            sqlite_delete(static_cast<TaskClosure<F>*>(self));
        }
    };

    /**
     * @brief Static entry trampoline for executing type-erased closure instances.
     * @param arg Pointer to `TaskClosureBase`.
     */
    static void task_closure_trampoline(void* arg) {
        TaskClosureBase* closure = static_cast<TaskClosureBase*>(arg);
        if (closure) {
            closure->invoke_fn(closure);
            closure->destroy_fn(closure);
        }
    }

    /**
     * @brief Static entry trampoline for raw function pointers.
     * @param arg Function pointer cast to `void*`.
     */
    static void raw_fn_trampoline(void* arg) {
        typedef void (*RawFn)();
        RawFn fn = reinterpret_cast<RawFn>(arg);
        if (fn) fn();
    }

public:
    /**
     * @brief Constructs an M:N Coroutine Scheduler with the specified number of worker threads.
     *
     * Allocates the stable C pool descriptor via SQLite's allocator (`sqlite_new`) and
     * starts the requested worker threads.
     *
     * @param num_workers Number of background worker threads (default 4; pass 0 for main-thread event loop mode).
     */
    inline explicit SqliteCoroScheduler(size_t num_workers = 4) : m_pool(nullptr) {
        m_pool = sqlite_new<sqlite3_coro_pool_t>();
        if (m_pool) {
            int rc = sqlite3_coro_pool_init(m_pool, static_cast<int>(num_workers));
            if (rc != SQLITE_OK) {
                sqlite_delete(m_pool);
                m_pool = nullptr;
            }
        }
    }

    /**
     * @brief Destructor. Automatically stops all workers, drains pending tasks, and frees memory.
     */
    inline ~SqliteCoroScheduler() {
        if (m_pool) {
            sqlite3_coro_pool_destroy(m_pool);
            sqlite_delete(m_pool);
            m_pool = nullptr;
        }
    }

    // Non-copyable
    SqliteCoroScheduler(const SqliteCoroScheduler&) = delete;
    SqliteCoroScheduler& operator=(const SqliteCoroScheduler&) = delete;

    /**
     * @brief Move constructor. Transfers ownership of the stable pool pointer in 1 CPU cycle.
     *
     * @param other Rvalue reference to scheduler being moved.
     */
    inline SqliteCoroScheduler(SqliteCoroScheduler&& other) noexcept : m_pool(other.m_pool) {
        other.m_pool = nullptr;
    }

    /**
     * @brief Move assignment operator. Safely tears down existing pool and assumes new ownership.
     *
     * @param other Rvalue reference to scheduler being moved.
     * @return Reference to this scheduler.
     */
    inline SqliteCoroScheduler& operator=(SqliteCoroScheduler&& other) noexcept {
        if (this != &other) {
            if (m_pool) {
                sqlite3_coro_pool_destroy(m_pool);
                sqlite_delete(m_pool);
            }
            m_pool = other.m_pool;
            other.m_pool = nullptr;
        }
        return *this;
    }

    /**
     * @brief Returns true if the scheduler pool is valid and initialized.
     * @return True if operational, false if moved-from or allocation failed.
     */
    inline bool is_valid() const noexcept { return m_pool != nullptr; }

    /**
     * @brief Returns the number of background worker threads.
     * @return Worker thread count.
     */
    inline size_t worker_count() const noexcept {
        return m_pool ? static_cast<size_t>(m_pool->num_workers) : 0;
    }

    /**
     * @brief Returns the count of pending and actively running tasks.
     * @return Pending task count.
     */
    inline size_t pending_tasks() const noexcept {
        return m_pool ? static_cast<size_t>(m_pool->pending_tasks) : 0;
    }

    /**
     * @brief Returns the underlying C `sqlite3_coro_pool_t*` pointer.
     * @return Pointer to internal C pool descriptor.
     */
    inline sqlite3_coro_pool_t* raw_pool() const noexcept {
        return m_pool;
    }

    // ========================================================================
    // TASK SPAWNING & SUBMISSION
    // ========================================================================

    /**
     * @brief Submits a callable closure (lambda, functor, capturing closure) into the scheduler.
     *
     * @tparam Callable Type of the callable.
     * @param callable The closure to execute cooperatively.
     * @param stack_size Stack size in bytes (0 for default 64 KB).
     * @return True if spawned successfully, false on allocation failure.
     */
    template <typename Callable>
    inline bool spawn(Callable&& callable, size_t stack_size = SQLITE3_CORO_DEFAULT_STACK_SIZE) {
        if (!m_pool) return false;

        typedef typename sqlite_remove_reference<Callable>::type CleanCallable;
        TaskClosure<CleanCallable>* closure = sqlite_new<TaskClosure<CleanCallable>>(sqlite_forward<Callable>(callable));
        if (!closure) return false;

        int rc = sqlite3_coro_pool_spawn(m_pool, task_closure_trampoline, closure, stack_size);
        if (rc != SQLITE_OK) {
            sqlite_delete(closure);
            return false;
        }
        return true;
    }

    /**
     * @brief Submits a raw C-style function pointer with no arguments.
     *
     * @param fn Function pointer of type `void (*)()`.
     * @param stack_size Stack size in bytes (0 for default 64 KB).
     * @return True if spawned successfully.
     */
    inline bool spawn(void (*fn)(), size_t stack_size = SQLITE3_CORO_DEFAULT_STACK_SIZE) {
        if (!m_pool || !fn) return false;
        return sqlite3_coro_pool_spawn(m_pool, raw_fn_trampoline, reinterpret_cast<void*>(fn), stack_size) == SQLITE_OK;
    }

    // ========================================================================
    // MAIN-THREAD / SYNCHRONOUS EXECUTION & POLLING
    // ========================================================================

    /**
     * @brief Steps a single ready coroutine task on the calling (main) thread.
     *
     * @return True if a task step was processed, false if the queue was empty.
     */
    inline bool poll_one() {
        if (!m_pool) return false;
        return sqlite3_coro_pool_poll_one(m_pool) != 0;
    }

    /**
     * @brief Drains and executes all ready tasks on the calling (main) thread until empty.
     *
     * @return Number of task step executions completed.
     */
    inline size_t run_until_empty() {
        if (!m_pool) return 0;
        return static_cast<size_t>(sqlite3_coro_pool_run_until_empty(m_pool));
    }

    /**
     * @brief Directly runs a coroutine task to completion on the calling (main) thread.
     *
     * Spawns the closure and synchronously drains the queue until the task finishes.
     *
     * @tparam Callable Type of the callable.
     * @param callable The closure to execute.
     * @param stack_size Stack size in bytes (0 for default 64 KB).
     */
    template <typename Callable>
    inline void run_local(Callable&& callable, size_t stack_size = SQLITE3_CORO_DEFAULT_STACK_SIZE) {
        spawn(sqlite_forward<Callable>(callable), stack_size);
        run_until_empty();
    }

    // ========================================================================
    // COOPERATIVE CONTEXT SWITCHING & SYNCHRONIZATION
    // ========================================================================

    /**
     * @brief Cooperatively yields the currently running task, placing it back on the ready queue.
     *
     * Safe to call from inside any task executing on the scheduler (worker thread or main thread).
     */
    static inline void yield() {
        sqlite3_coro_pool_yield();
    }

    /**
     * @brief Blocks the calling thread until all pending and active tasks have finished.
     */
    inline void wait_all() {
        if (m_pool) {
            sqlite3_coro_pool_wait(m_pool);
        }
    }

    /**
     * @brief Shuts down all worker threads and releases scheduler resources.
     */
    inline void shutdown() {
        if (m_pool) {
            sqlite3_coro_pool_destroy(m_pool);
            sqlite_delete(m_pool);
            m_pool = nullptr;
        }
    }

    // ========================================================================
    // PROCESS-WIDE GLOBAL SINGLETON WITH ATOMIC REF-COUNTING
    // ========================================================================

private:
    struct GlobalState {
        SqliteCoroScheduler*   scheduler;
        SqliteAtomicInt        ref_count;
        sqlite3_thread_mutex_t lock;
    };

    static inline GlobalState& global_state() {
        static GlobalState state;
        static bool initialized = false;
        if (!initialized) {
            sqlite3_thread_mutex_init(&state.lock);
            state.ref_count.store(0);
            state.scheduler = nullptr;
            initialized = true;
        }
        return state;
    }

public:
    /**
     * @brief Acquires the process-wide global scheduler, creating it if needed.
     *
     * Atomically increments the reference count. Shared across all loaded database connections.
     *
     * @param num_workers Worker thread count if creating the pool (default 4).
     * @return Pointer to the shared global `SqliteCoroScheduler`.
     */
    static inline SqliteCoroScheduler* acquire_global(size_t num_workers = 4) {
        GlobalState& state = global_state();
        sqlite3_thread_mutex_lock(&state.lock);

        if (!state.scheduler) {
            state.scheduler = sqlite_new<SqliteCoroScheduler>(num_workers);
        }
        state.ref_count++;

        SqliteCoroScheduler* ret = state.scheduler;
        sqlite3_thread_mutex_unlock(&state.lock);
        return ret;
    }

    /**
     * @brief Releases a reference to the global scheduler.
     *
     * When the reference count drops to 0, automatically shuts down the pool and frees memory.
     */
    static inline void release_global() {
        GlobalState& state = global_state();
        sqlite3_thread_mutex_lock(&state.lock);

        if (state.ref_count > 0) {
            state.ref_count--;
            if (state.ref_count == 0 && state.scheduler) {
                sqlite_delete(state.scheduler);
                state.scheduler = nullptr;
            }
        }

        sqlite3_thread_mutex_unlock(&state.lock);
    }

    /**
     * @brief Forcibly shuts down the global scheduler immediately (e.g., during DLL unload).
     */
    static inline void shutdown_global() {
        GlobalState& state = global_state();
        sqlite3_thread_mutex_lock(&state.lock);

        if (state.scheduler) {
            sqlite_delete(state.scheduler);
            state.scheduler = nullptr;
            state.ref_count = 0;
        }

        sqlite3_thread_mutex_unlock(&state.lock);
    }
};

/**
 * @brief Type alias: `SqliteFiberPool` is an identical alias for `SqliteCoroScheduler`.
 */
typedef SqliteCoroScheduler SqliteFiberPool;

// ============================================================================
// STANDALONE TEMPLATE SPAWN HELPERS
// ============================================================================

/**
 * @brief Standalone template function to spawn a task into a `SqliteCoroScheduler` instance.
 *
 * @tparam Callable Closure type (capturing lambda, functor, or function pointer).
 * @param sched Reference to `SqliteCoroScheduler`.
 * @param callable The task closure to execute.
 * @param stack_size Stack size in bytes (defaults to `SQLITE3_CORO_DEFAULT_STACK_SIZE` = 64 KB).
 * @return True on success, false on failure.
 */
template <typename Callable>
inline bool sqlite_coro_spawn(SqliteCoroScheduler& sched, Callable&& callable, size_t stack_size = SQLITE3_CORO_DEFAULT_STACK_SIZE) {
    return sched.spawn(sqlite_forward<Callable>(callable), stack_size);
}

/**
 * @brief Standalone template function to spawn a task into a `SqliteCoroScheduler*` pointer.
 *
 * @tparam Callable Closure type (capturing lambda, functor, or function pointer).
 * @param sched Pointer to `SqliteCoroScheduler`.
 * @param callable The task closure to execute.
 * @param stack_size Stack size in bytes (defaults to `SQLITE3_CORO_DEFAULT_STACK_SIZE` = 64 KB).
 * @return True on success, false on failure.
 */
template <typename Callable>
inline bool sqlite_coro_spawn(SqliteCoroScheduler* sched, Callable&& callable, size_t stack_size = SQLITE3_CORO_DEFAULT_STACK_SIZE) {
    return sched ? sched->spawn(sqlite_forward<Callable>(callable), stack_size) : false;
}

/**
 * @brief Standalone template function to spawn a task into a raw C `sqlite3_coro_pool_t*` handle.
 *
 * Provides a C++ closure interface directly over raw C `sqlite3_coro_pool_t*` pointers.
 *
 * @tparam Callable Closure type.
 * @param pool Pointer to raw C `sqlite3_coro_pool_t`.
 * @param callable The task closure to execute.
 * @param stack_size Stack size in bytes (defaults to `SQLITE3_CORO_DEFAULT_STACK_SIZE` = 64 KB).
 * @return True on success, false on failure.
 */
template <typename Callable>
inline bool sqlite_coro_spawn(sqlite3_coro_pool_t* pool, Callable&& callable, size_t stack_size = SQLITE3_CORO_DEFAULT_STACK_SIZE) {
    if (!pool) return false;
    typedef typename sqlite_remove_reference<Callable>::type CleanCallable;
    SqliteCoroScheduler::TaskClosure<CleanCallable>* closure = 
        sqlite_new<SqliteCoroScheduler::TaskClosure<CleanCallable>>(sqlite_forward<Callable>(callable));
    if (!closure) return false;

    int rc = sqlite3_coro_pool_spawn(pool, SqliteCoroScheduler::task_closure_trampoline, closure, stack_size);
    if (rc != SQLITE_OK) {
        sqlite_delete(closure);
        return false;
    }
    return true;
}

/**
 * @brief Standalone template function to spawn a task with a compile-time fixed stack size.
 *
 * @tparam StackSize Explicit stack size in bytes (e.g., `32 * 1024` or `128 * 1024`).
 * @tparam Sched Scheduler reference or pointer.
 * @tparam Callable Closure type.
 * @param sched Target scheduler instance or pointer.
 * @param callable The task closure to execute.
 * @return True on success, false on failure.
 */
template <size_t StackSize, typename Sched, typename Callable>
inline bool sqlite_coro_spawn_stack(Sched&& sched, Callable&& callable) {
    return sqlite_coro_spawn(sqlite_forward<Sched>(sched), sqlite_forward<Callable>(callable), StackSize);
}

#endif // SQLITE3_CORO_SCHED_HPP
