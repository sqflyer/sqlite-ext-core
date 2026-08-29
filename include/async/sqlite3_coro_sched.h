#ifndef SQLITE3_CORO_SCHED_H
#define SQLITE3_CORO_SCHED_H

/**
 * @file sqlite3_coro_sched.h
 * @brief Pure C99 M:N Cooperative Coroutine Scheduler & Worker Pool for SQLite extensions.
 *
 * ## Architecture Overview
 * This header implements a high-throughput, low-latency M:N cooperative task scheduler
 * tailored specifically for SQLite loadable extensions and embedded database workloads:
 *
 * 1. **M:N Cooperative Execution Model**:
 *    - Schedules $M$ cooperative stackful coroutines (fibers) across $N$ OS worker threads.
 *    - When $N = 0$, operates strictly in single-threaded / main-thread event loop mode
 *      (suitable for WebAssembly/Emscripten, TVFs, and thread-affine SQLite operations).
 *    - When $N > 0$, worker threads pull ready tasks from a thread-safe synchronized FIFO queue.
 *
 * 2. **Cooperative Context Switching & Yielding**:
 *    - Tasks yield cooperatively via `sqlite3_coro_pool_yield()`.
 *    - The yielding fiber suspends execution and transfers control back to the hosting worker thread.
 *    - The worker thread safely places the suspended task back into the scheduler queue and signals
 *      waiting workers, enabling dynamic work distribution across threads.
 *
 * 3. **Deterministic Memory Tracking & Zero CRT Dependencies**:
 *    - All internal heap allocations (queues, task containers, fiber stacks) route strictly
 *      through SQLite's memory manager (`sqlite3_malloc64` / `sqlite3_free`), ensuring 100%
 *      accounting in SQLite memory statistics (`sqlite3_memory_used()`).
 *    - Fully operational in freestanding, `-nostdlib`, and `-fno-exceptions` builds.
 *
 * 4. **Synchronous Polling & Step Execution**:
 *    - Supports manual task-stepping (`sqlite3_coro_pool_poll_one`) and draining
 *      (`sqlite3_coro_pool_run_until_empty`) for embedding in database query loops.
 */

#include <sqlite3.h>
#include <stddef.h>
#include <stdint.h>
#include "sqlite3_coro.h"
#include "sqlite3_thread.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// DATA STRUCTURES
// ============================================================================

typedef struct sqlite3_coro_pool_t sqlite3_coro_pool_t;

/**
 * @brief Alias for coroutine entry function pointer signature: `void (*)(void* arg)`.
 */
typedef sqlite3_coro_entry_t sqlite3_coro_fn;

/**
 * @struct sqlite3_coro_task_t
 * @brief Internal descriptor representing a scheduled fiber task within the M:N scheduler.
 */
typedef struct sqlite3_coro_task_t {
    sqlite3_coro_t              coro;          /**< Underlying stackful fiber context handle. */
    sqlite3_coro_entry_t        fn;            /**< User-supplied task entry function. */
    void*                       arg;           /**< User-supplied argument payload. */
    sqlite3_coro_pool_t*        pool;          /**< Pointer to the owning scheduler pool. */
    struct sqlite3_coro_task_t* next;          /**< Intrusive pointer for FIFO queue linking. */
} sqlite3_coro_task_t;

/**
 * @struct sqlite3_coro_pool_t
 * @brief Thread-safe M:N Coroutine Scheduler and Worker Thread Pool.
 */
struct sqlite3_coro_pool_t {
    sqlite3_thread_t*       workers;           /**< Dynamic array of background worker thread handles. */
    int                     num_workers;       /**< Total count of background worker threads ($0$ = main-thread only). */
    sqlite3_coro_task_t*    queue_head;        /**< Pointer to the head of the FIFO ready queue. */
    sqlite3_coro_task_t*    queue_tail;        /**< Pointer to the tail of the FIFO ready queue. */
    int                     pending_tasks;     /**< Counter of active and queued tasks pending completion. */
    int                     is_running;        /**< Status flag ($1$ = running, $0$ = shutting down). */
    sqlite3_thread_mutex_t  lock;              /**< Synchronization mutex protecting task queue and counters. */
    sqlite3_cond_t          cond_work;         /**< Condition variable signaling available work for workers. */
    sqlite3_cond_t          cond_done;         /**< Condition variable signaling completion of all pending tasks. */
};

/**
 * @var g_active_pool_task
 * @brief Thread-local pointer tracking the currently active fiber task on the current OS thread.
 */
#if defined(_MSC_VER)
    static __declspec(thread) sqlite3_coro_task_t* g_active_pool_task = NULL;
#else
    static __thread sqlite3_coro_task_t* g_active_pool_task = NULL;
#endif

// ============================================================================
// INTERNAL WORKER & TRAMPOLINE IMPLEMENTATION
// ============================================================================

/**
 * @brief Trampoline executing the user's task callback on the allocated fiber stack.
 *
 * @param arg Pointer to the `sqlite3_coro_task_t` descriptor.
 */
static inline void sqlite3_coro_task_trampoline(void* arg) {
    sqlite3_coro_task_t* task = (sqlite3_coro_task_t*)arg;
    if (task && task->fn) {
        task->fn(task->arg);
    }
}

/**
 * @brief Worker thread main loop executing scheduled fibers in the M:N pool.
 *
 * Each worker thread converts itself to a primary fiber (with `FIBER_FLAG_FLOAT_SWITCH`)
 * so that it can perform hardware context switches to and from task fibers.
 *
 * Workers wait on `pool->cond_work` until a task is enqueued. When a task runs:
 * - If the task runs to completion: the fiber and task container are destroyed,
 *   and `pool->pending_tasks` is decremented. If `pending_tasks == 0`, `cond_done` is signaled.
 * - If the task yields cooperatively: it is re-appended to the tail of the ready queue
 *   and `cond_work` is signaled to wake up any idle workers to resume it.
 *
 * @param arg Pointer to the parent `sqlite3_coro_pool_t` instance.
 * @return NULL upon thread termination.
 */
static inline void* sqlite3_coro_pool_worker_loop(void* arg) {
    sqlite3_coro_pool_t* pool = (sqlite3_coro_pool_t*)arg;
    if (!pool) return NULL;

#if defined(_WIN32) || defined(_WIN64)
#ifndef FIBER_FLAG_FLOAT_SWITCH
#define FIBER_FLAG_FLOAT_SWITCH 0x1
#endif
    /*
     * STEP 1: CONVERT OS THREAD TO PRIMARY FIBER
     * On Windows, a thread cannot call SwitchToFiber() unless it is converted
     * into a fiber itself. We specify FIBER_FLAG_FLOAT_SWITCH (0x1) to ensure
     * that the hardware floating-point and SSE registers (XMM6-XMM15) are
     * preserved during fiber transitions under compiler optimization (-O2).
     */
    void* cur_fiber = GetCurrentFiber();
    if (!cur_fiber || (uintptr_t)cur_fiber < (uintptr_t)0x10000) {
        cur_fiber = ConvertThreadToFiberEx(NULL, FIBER_FLAG_FLOAT_SWITCH);
        if (!cur_fiber) {
            cur_fiber = ConvertThreadToFiber(NULL);
        }
    }
#endif

    while (1) {
        sqlite3_coro_task_t* task = NULL;

        /*
         * STEP 2: WAIT FOR WORK & DEQUEUE FROM FIFO READY QUEUE
         * Lock the pool mutex and wait on cond_work while the queue is empty
         * and the pool is active.
         */
        sqlite3_thread_mutex_lock(&pool->lock);
        while (pool->is_running && pool->queue_head == NULL) {
            sqlite3_cond_wait(&pool->cond_work, &pool->lock);
        }

        // Check if pool is shutting down and all tasks have been drained
        if (!pool->is_running && pool->queue_head == NULL) {
            sqlite3_thread_mutex_unlock(&pool->lock);
            break;
        }

        // Dequeue the next task from the FIFO head
        task = pool->queue_head;
        if (task) {
            pool->queue_head = task->next;
            if (pool->queue_head == NULL) {
                pool->queue_tail = NULL;
            }
            task->next = NULL;
        }
        sqlite3_thread_mutex_unlock(&pool->lock);

        if (!task) continue;

        /*
         * STEP 3: RESUME FIBER TASK
         * Bind the active task pointer to thread-local storage for state tracking,
         * then perform a stackful context switch into the task fiber.
         */
        sqlite3_coro_task_t* prev_task = g_active_pool_task;
        g_active_pool_task = task;

        sqlite3_coro_resume(&task->coro);

        g_active_pool_task = prev_task;

        /*
         * STEP 4: PROCESS TASK EXECUTION RESULT
         * Evaluate whether the fiber completed execution or yielded cooperatively.
         */
        if (sqlite3_coro_is_done(&task->coro)) {
            // Case 4A: Task completed normally -> destroy fiber context and free task container
            sqlite3_coro_destroy(&task->coro);
            sqlite3_free(task);

            sqlite3_thread_mutex_lock(&pool->lock);
            pool->pending_tasks--;
            if (pool->pending_tasks == 0) {
                // All pending tasks have finished -> broadcast completion signal
                sqlite3_cond_broadcast(&pool->cond_done);
            }
            sqlite3_thread_mutex_unlock(&pool->lock);
        } else {
            // Case 4B: Task yielded cooperatively -> inspect pool state and re-enqueue
            sqlite3_thread_mutex_lock(&pool->lock);
            if (!pool->is_running) {
                // Pool is shutting down: discard and free suspended task
                sqlite3_thread_mutex_unlock(&pool->lock);
                sqlite3_coro_destroy(&task->coro);
                sqlite3_free(task);

                sqlite3_thread_mutex_lock(&pool->lock);
                pool->pending_tasks--;
                if (pool->pending_tasks == 0) {
                    sqlite3_cond_broadcast(&pool->cond_done);
                }
                sqlite3_cond_broadcast(&pool->cond_work);
                sqlite3_thread_mutex_unlock(&pool->lock);
                break;
            } else {
                // Re-enqueue task at the tail of the ready queue for subsequent scheduling
                task->next = NULL;
                if (pool->queue_tail) {
                    pool->queue_tail->next = task;
                    pool->queue_tail = task;
                } else {
                    pool->queue_head = task;
                    pool->queue_tail = task;
                }
                sqlite3_cond_broadcast(&pool->cond_work);
                sqlite3_thread_mutex_unlock(&pool->lock);
            }
        }
    }

    /*
     * STEP 5: WORKER THREAD TERMINATION
     * Returns NULL upon clean worker loop exit.
     */
    return NULL;
}

// ============================================================================
// PUBLIC C API
// ============================================================================

/**
 * @brief Initializes an M:N Coroutine Scheduler and Worker Thread Pool.
 *
 * Sets up synchronization mutexes, condition variables, and starts the requested
 * number of background worker threads.
 *
 * @param pool Pointer to uninitialized `sqlite3_coro_pool_t` structure.
 * @param num_workers Number of background OS worker threads (pass 0 for main-thread event loop mode).
 * @return `SQLITE_OK` on success, `SQLITE_NOMEM` on allocation failure, or `SQLITE_MISUSE` if pool is NULL.
 */
static inline int sqlite3_coro_pool_init(sqlite3_coro_pool_t* pool, int num_workers) {
    if (!pool) return SQLITE_MISUSE;

    if (num_workers < 0) num_workers = 0;

    pool->num_workers = num_workers;
    pool->queue_head = NULL;
    pool->queue_tail = NULL;
    pool->pending_tasks = 0;
    pool->is_running = 1;
    pool->workers = NULL;

    sqlite3_thread_mutex_init(&pool->lock);
    sqlite3_cond_init(&pool->cond_work);
    sqlite3_cond_init(&pool->cond_done);

    if (num_workers > 0) {
        pool->workers = (sqlite3_thread_t*)sqlite3_malloc64(sizeof(sqlite3_thread_t) * (size_t)num_workers);
        if (!pool->workers) {
            sqlite3_thread_mutex_destroy(&pool->lock);
            sqlite3_cond_destroy(&pool->cond_work);
            sqlite3_cond_destroy(&pool->cond_done);
            return SQLITE_NOMEM;
        }

        for (int i = 0; i < num_workers; ++i) {
            int rc = sqlite3_thread_create(&pool->workers[i], sqlite3_coro_pool_worker_loop, pool);
            if (rc != SQLITE_OK) {
                pool->is_running = 0;
                sqlite3_cond_broadcast(&pool->cond_work);
                for (int j = 0; j < i; ++j) {
                    sqlite3_thread_join(&pool->workers[j], NULL);
                }
                sqlite3_free(pool->workers);
                pool->workers = NULL;
                return rc;
            }
        }
    }

    return SQLITE_OK;
}

/**
 * @brief Submits a new coroutine task into the scheduler pool.
 *
 * Allocates a task descriptor and fiber context with the specified stack size,
 * enqueuing it onto the ready queue.
 *
 * @param pool Pointer to active `sqlite3_coro_pool_t`.
 * @param fn Task entry function of type `void (*)(void* arg)`.
 * @param arg User context pointer passed to `fn`.
 * @param stack_size Fiber stack size in bytes (pass 0 for default 64 KB).
 * @return `SQLITE_OK` on success, `SQLITE_NOMEM` on allocation failure, or error code.
 */
static inline int sqlite3_coro_pool_spawn(sqlite3_coro_pool_t* pool, sqlite3_coro_fn fn, void* arg, size_t stack_size) {
    if (!pool || !fn) return SQLITE_MISUSE;

    sqlite3_coro_task_t* task = (sqlite3_coro_task_t*)sqlite3_malloc64(sizeof(sqlite3_coro_task_t));
    if (!task) return SQLITE_NOMEM;

    task->fn = fn;
    task->arg = arg;
    task->pool = pool;
    task->next = NULL;

    int rc = sqlite3_coro_create(&task->coro, stack_size, sqlite3_coro_task_trampoline, task);
    if (rc != SQLITE_OK) {
        sqlite3_free(task);
        return rc;
    }

    sqlite3_thread_mutex_lock(&pool->lock);
    if (!pool->is_running) {
        sqlite3_thread_mutex_unlock(&pool->lock);
        sqlite3_coro_destroy(&task->coro);
        sqlite3_free(task);
        return SQLITE_MISUSE;
    }

    if (pool->queue_tail) {
        pool->queue_tail->next = task;
        pool->queue_tail = task;
    } else {
        pool->queue_head = task;
        pool->queue_tail = task;
    }
    pool->pending_tasks++;

    sqlite3_cond_broadcast(&pool->cond_work);
    sqlite3_thread_mutex_unlock(&pool->lock);

    return SQLITE_OK;
}

/**
 * @brief Cooperatively yields the currently running task, transferring control back to scheduler.
 *
 * Suspends the calling fiber's execution. If executing on a background worker thread, the
 * hosting worker automatically re-enqueues the task at the tail of the ready queue and wakes
 * up available workers. If executing on the main thread via `poll_one()`, control returns to
 * the polling loop.
 *
 * @note Safe to invoke from anywhere within the coroutine task's call stack. Calling outside
 * an active coroutine is a safe no-op.
 */
static inline void sqlite3_coro_pool_yield(void) {
    sqlite3_coro_yield();
}

/**
 * @brief Steps a single ready task on the calling (main) thread.
 *
 * Synchronously dequeues one task from the head of the ready queue, executes it until it
 * either completes or cooperatively yields, and updates scheduler queues accordingly.
 *
 * @param pool Pointer to initialized `sqlite3_coro_pool_t` instance.
 * @return `1` if a task step was processed, `0` if the ready queue was empty or pool is NULL.
 */
static inline int sqlite3_coro_pool_poll_one(sqlite3_coro_pool_t* pool) {
    if (!pool) return 0;

    sqlite3_coro_task_t* task = NULL;

    /*
     * STEP 1: DEQUEUE A SINGLE TASK FROM FIFO HEAD
     * Safely lock the pool mutex and extract the first ready task.
     */
    sqlite3_thread_mutex_lock(&pool->lock);
    if (pool->queue_head) {
        task = pool->queue_head;
        pool->queue_head = task->next;
        if (pool->queue_head == NULL) {
            pool->queue_tail = NULL;
        }
        task->next = NULL;
    }
    sqlite3_thread_mutex_unlock(&pool->lock);

    if (!task) return 0;

    /*
     * STEP 2: RESUME TASK FIBER CONTEXT
     * Set the thread-local active task pointer and perform a synchronous context switch.
     */
    sqlite3_coro_task_t* prev = g_active_pool_task;
    g_active_pool_task = task;

    sqlite3_coro_resume(&task->coro);

    g_active_pool_task = prev;

    /*
     * STEP 3: HANDLE TASK COMPLETION OR RE-QUEUE
     */
    if (sqlite3_coro_is_done(&task->coro)) {
        // Case 3A: Task finished execution -> free fiber context and notify waiters
        sqlite3_coro_destroy(&task->coro);
        sqlite3_free(task);

        sqlite3_thread_mutex_lock(&pool->lock);
        pool->pending_tasks--;
        if (pool->pending_tasks == 0) {
            sqlite3_cond_broadcast(&pool->cond_done);
        }
        sqlite3_thread_mutex_unlock(&pool->lock);
    } else {
        // Case 3B: Task yielded cooperatively -> re-insert at tail of ready queue
        sqlite3_thread_mutex_lock(&pool->lock);
        task->next = NULL;
        if (pool->queue_tail) {
            pool->queue_tail->next = task;
            pool->queue_tail = task;
        } else {
            pool->queue_head = task;
            pool->queue_tail = task;
        }
        sqlite3_thread_mutex_unlock(&pool->lock);
    }

    return 1;
}

/**
 * @brief Drains and executes all ready tasks on the calling (main) thread until empty.
 *
 * Runs a synchronous poll loop repeatedly stepping tasks until the ready queue is completely
 * drained. Essential for single-threaded / WebAssembly environments, TVFs, and synchronous SQLite operations.
 *
 * @param pool Pointer to initialized `sqlite3_coro_pool_t` instance.
 * @return Total number of task step executions processed.
 */
static inline int sqlite3_coro_pool_run_until_empty(sqlite3_coro_pool_t* pool) {
    int count = 0;
    while (sqlite3_coro_pool_poll_one(pool)) {
        count++;
    }
    return count;
}

/**
 * @brief Blocks the calling thread until all pending and active tasks have finished execution.
 *
 * Enters a condition variable wait on `pool->cond_done`. Uses periodic timed waits to ensure
 * responsive shutdown and deadlock immunity.
 *
 * @param pool Pointer to initialized `sqlite3_coro_pool_t` instance.
 */
static inline void sqlite3_coro_pool_wait(sqlite3_coro_pool_t* pool) {
    if (!pool) return;

    sqlite3_thread_mutex_lock(&pool->lock);
    while (pool->pending_tasks > 0) {
        sqlite3_cond_timedwait(&pool->cond_done, &pool->lock, 25);
    }
    sqlite3_thread_mutex_unlock(&pool->lock);
}

/**
 * @brief Stops all background worker threads, drains pending queues, and frees resources.
 *
 * Signals all worker threads to terminate, waits for their exit via `sqlite3_thread_join`,
 * cancels and frees all unexecuted tasks remaining in the queue, and destroys synchronization primitives.
 *
 * @param pool Pointer to initialized `sqlite3_coro_pool_t` instance.
 */
static inline void sqlite3_coro_pool_destroy(sqlite3_coro_pool_t* pool) {
    if (!pool) return;

    sqlite3_thread_mutex_lock(&pool->lock);
    pool->is_running = 0;
    sqlite3_cond_broadcast(&pool->cond_work);
    sqlite3_thread_mutex_unlock(&pool->lock);

    // Join and terminate all background worker threads
    if (pool->workers && pool->num_workers > 0) {
        for (int i = 0; i < pool->num_workers; ++i) {
            sqlite3_thread_join(&pool->workers[i], NULL);
        }
        sqlite3_free(pool->workers);
        pool->workers = NULL;
    }

    // Clean up any remaining unexecuted tasks in the ready queue
    sqlite3_coro_task_t* curr = pool->queue_head;
    while (curr) {
        sqlite3_coro_task_t* next = curr->next;
        sqlite3_coro_destroy(&curr->coro);
        sqlite3_free(curr);
        curr = next;
    }
    pool->queue_head = NULL;
    pool->queue_tail = NULL;
    pool->pending_tasks = 0;

    sqlite3_thread_mutex_destroy(&pool->lock);
    sqlite3_cond_destroy(&pool->cond_work);
    sqlite3_cond_destroy(&pool->cond_done);
}

// ============================================================================
// EXTENSION-PRESENCE PROCESS-WIDE GLOBAL POOL
// ============================================================================

/**
 * @struct sqlite3_coro_global_state_t
 * @brief Internal state tracking the process-wide shared coroutine worker pool.
 *
 * Implements a reference-counted singleton ensuring that multiple loaded database
 * connections within the same process share a single coroutine worker pool.
 */
typedef struct {
    sqlite3_coro_pool_t*   pool;              /**< Pointer to the shared worker pool instance. */
    int                    ref_count;         /**< Count of active database connections referencing the pool. */
    sqlite3_thread_mutex_t lock;              /**< Mutex protecting singleton creation, destruction, and ref counts. */
    int                    lock_initialized;  /**< Flag indicating if the singleton mutex has been initialized. */
} sqlite3_coro_global_state_t;

/**
 * @brief Retrieves the static singleton descriptor for the process-wide global pool.
 *
 * @return Pointer to static `sqlite3_coro_global_state_t` instance.
 */
static inline sqlite3_coro_global_state_t* sqlite3_coro_get_global_state(void) {
    static sqlite3_coro_global_state_t state;
    static int initialized = 0;
    if (!initialized) {
        memset(&state, 0, sizeof(state));
        initialized = 1;
    }
    return &state;
}

/**
 * @brief Acquires the process-wide extension presence coroutine pool.
 *
 * Lazily allocates and initializes the shared M:N worker pool upon the first database
 * connection's request, incrementing the reference count. Subsequent database connections
 * reuse the exact same pool and worker threads, eliminating redundant OS thread creation.
 *
 * @param num_workers Number of background OS worker threads to allocate if creating the pool (defaults to 4).
 * @return Pointer to shared `sqlite3_coro_pool_t`, or `NULL` on memory allocation error.
 */
static inline sqlite3_coro_pool_t* sqlite3_coro_pool_acquire_global(int num_workers) {
    if (num_workers <= 0) num_workers = 4;
    sqlite3_coro_global_state_t* state = sqlite3_coro_get_global_state();

    if (!state->lock_initialized) {
        sqlite3_thread_mutex_init(&state->lock);
        state->lock_initialized = 1;
    }

    sqlite3_thread_mutex_lock(&state->lock);

    /*
     * STEP 1: LAZY POOL INITIALIZATION
     * If no global pool exists, allocate descriptor via sqlite3_malloc64 and start workers.
     */
    if (!state->pool) {
        state->pool = (sqlite3_coro_pool_t*)sqlite3_malloc64(sizeof(sqlite3_coro_pool_t));
        if (!state->pool) {
            sqlite3_thread_mutex_unlock(&state->lock);
            return NULL;
        }
        int rc = sqlite3_coro_pool_init(state->pool, num_workers);
        if (rc != SQLITE_OK) {
            sqlite3_free(state->pool);
            state->pool = NULL;
            sqlite3_thread_mutex_unlock(&state->lock);
            return NULL;
        }
    }

    /*
     * STEP 2: INCREMENT CONNECTION REFERENCE COUNT
     */
    state->ref_count++;
    sqlite3_coro_pool_t* ret = state->pool;
    sqlite3_thread_mutex_unlock(&state->lock);
    return ret;
}

/**
 * @brief Releases a reference to the process-wide extension presence pool.
 *
 * Decrements the connection reference count. When the count drops to 0 (all database
 * connections have closed), automatically terminates all background worker threads and
 * frees the pool memory.
 */
static inline void sqlite3_coro_pool_release_global(void) {
    sqlite3_coro_global_state_t* state = sqlite3_coro_get_global_state();
    if (!state->lock_initialized) return;

    sqlite3_thread_mutex_lock(&state->lock);
    if (state->ref_count > 0) {
        state->ref_count--;
        if (state->ref_count == 0 && state->pool) {
            // Last connection closed -> destroy worker threads and free heap memory
            sqlite3_coro_pool_destroy(state->pool);
            sqlite3_free(state->pool);
            state->pool = NULL;
        }
    }
    sqlite3_thread_mutex_unlock(&state->lock);
}

/**
 * @brief Forcibly shuts down the process-wide extension presence pool immediately.
 *
 * Terminates all worker threads and frees pool memory regardless of the current reference
 * count. Intended for use during extension module unloads (`sqlite3_extension_init` teardown / `sqlite3_close_v2`).
 */
static inline void sqlite3_coro_pool_shutdown_global(void) {
    sqlite3_coro_global_state_t* state = sqlite3_coro_get_global_state();
    if (!state->lock_initialized) return;

    sqlite3_thread_mutex_lock(&state->lock);
    if (state->pool) {
        sqlite3_coro_pool_destroy(state->pool);
        sqlite3_free(state->pool);
        state->pool = NULL;
        state->ref_count = 0;
    }
    sqlite3_thread_mutex_unlock(&state->lock);
}

#ifdef __cplusplus
}
#endif

#endif /* SQLITE3_CORO_SCHED_H */
