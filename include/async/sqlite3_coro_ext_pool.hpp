#ifndef SQLITE3_CORO_EXT_POOL_HPP
#define SQLITE3_CORO_EXT_POOL_HPP

/**
 * @file sqlite3_coro_ext_pool.hpp
 * @brief Type-safe, isolated, per-extension coroutine pool manager for SQLite extensions (C++11/C++20).
 *
 * ## Architecture Overview
 * Provides extension-presence coroutine worker pool management with zero-overhead type isolation:
 *
 * 1. **Tagged Template Isolation (`SqliteExtCoroPool<Tag>`)**:
 *    - Each extension defines its own tag type (e.g. `struct VectorExtTag {}`).
 *    - C++ template instantiation automatically creates an isolated static worker pool and
 *      reference counter per extension tag at zero runtime overhead.
 *    - Extension A (`SqliteExtCoroPool<VectorTag>`) and Extension B (`SqliteExtCoroPool<CryptoTag>`)
 *      run in completely separate worker pools with their own thread counts and lifecycles.
 *
 * 2. **Multi-Database Shared Connection Presence**:
 *    - All database connections loading the same extension share its dedicated worker pool.
 *    - Automatically ref-counted: drains and tears down worker threads when the last database closes.
 *
 * 3. **Freestanding & Standard-Library Free**:
 *    - Zero `<functional>` or `<thread>` dependencies (`-nostdlib++` safe).
 *    - Type-erased lambda closure dispatch with `sqlite_coro_spawn`.
 */

#include "sqlite3_coro_sched.hpp"
#include "sqlite3_coro_ext_pool.h"

// ============================================================================
// 1. TYPE-SAFE TAGGED EXTENSION COROUTINE POOL
// ============================================================================

/**
 * @class SqliteExtCoroPool
 * @brief Type-safe, isolated, reference-counted coroutine worker pool per extension tag.
 *
 * @tparam ExtensionTag Unique type identifying the extension (e.g., `struct MyExtTag`).
 */
template <typename ExtensionTag = void>
class SqliteExtCoroPool {
private:
    struct State {
        SqliteCoroScheduler*   scheduler;
        SqliteAtomicInt        ref_count;
        sqlite3_thread_mutex_t lock;
        bool                   lock_initialized;

        State() : scheduler(nullptr), ref_count(0), lock_initialized(false) {
            memset(&lock, 0, sizeof(lock));
        }
    };

    static inline State& get_state() {
        static State s;
        return s;
    }

public:
    /**
     * @brief Acquires (or creates) the dedicated worker pool for this extension tag.
     *
     * @param num_workers Number of background OS worker threads (defaults to 4).
     * @return Pointer to `SqliteCoroScheduler`, or nullptr on memory failure.
     */
    static inline SqliteCoroScheduler* acquire(size_t num_workers = 4) {
        State& s = get_state();
        if (!s.lock_initialized) {
            sqlite3_thread_mutex_init(&s.lock);
            s.lock_initialized = true;
        }

        sqlite3_thread_mutex_lock(&s.lock);
        if (!s.scheduler) {
            s.scheduler = sqlite_new<SqliteCoroScheduler>(num_workers);
            if (!s.scheduler) {
                sqlite3_thread_mutex_unlock(&s.lock);
                return nullptr;
            }
        }
        s.ref_count++;
        SqliteCoroScheduler* ret = s.scheduler;
        sqlite3_thread_mutex_unlock(&s.lock);
        return ret;
    }

    /**
     * @brief Releases a reference from an active database connection.
     *
     * Automatically destroys and frees the worker pool when the reference count reaches 0.
     */
    static inline void release() {
        State& s = get_state();
        if (!s.lock_initialized) return;

        sqlite3_thread_mutex_lock(&s.lock);
        if (s.ref_count > 0) {
            s.ref_count--;
            if (s.ref_count == 0 && s.scheduler) {
                sqlite_delete(s.scheduler);
                s.scheduler = nullptr;
            }
        }
        sqlite3_thread_mutex_unlock(&s.lock);
    }

    /**
     * @brief Synchronously waits until all queued/active tasks in this extension pool complete.
     */
    static inline void wait_all() {
        State& s = get_state();
        if (!s.lock_initialized) return;

        sqlite3_thread_mutex_lock(&s.lock);
        SqliteCoroScheduler* sched = s.scheduler;
        sqlite3_thread_mutex_unlock(&s.lock);

        if (sched) {
            sched->wait_all();
        }
    }

    /**
     * @brief Forcibly shuts down the worker pool immediately.
     */
    static inline void shutdown() {
        State& s = get_state();
        if (!s.lock_initialized) return;

        sqlite3_thread_mutex_lock(&s.lock);
        if (s.scheduler) {
            sqlite_delete(s.scheduler);
            s.scheduler = nullptr;
            s.ref_count = 0;
        }
        sqlite3_thread_mutex_unlock(&s.lock);
    }

    /**
     * @brief Returns current active database connection count sharing this extension pool.
     */
    static inline int ref_count() {
        State& s = get_state();
        return s.ref_count.load();
    }

    /**
     * @brief Returns the underlying scheduler pointer (or nullptr if not active).
     */
    static inline SqliteCoroScheduler* get() {
        State& s = get_state();
        return s.scheduler;
    }
};

/**
 * @brief Alias for SqliteExtCoroPool.
 */
template <typename ExtensionTag = void>
using SqliteExtensionCoroPool = SqliteExtCoroPool<ExtensionTag>;

// ============================================================================
// 2. TEMPLATE SPAWN HELPER FOR EXTENSION PRESENCE POOLS
// ============================================================================

/**
 * @brief Spawns a stateful C++11 capturing closure into a tagged extension pool.
 *
 * @tparam ExtensionTag Unique type tag identifying the extension.
 * @tparam F Callable type (lambda, functor, function pointer).
 * @param f The callable to execute asynchronously on a fiber.
 * @param stack_size Stack size in bytes (defaults to 64 KB).
 * @return `true` if successfully enqueued, `false` on allocation error.
 */
template <typename ExtensionTag = void, typename F>
inline bool sqlite_coro_ext_spawn(F&& f, size_t stack_size = SQLITE3_CORO_DEFAULT_STACK_SIZE) {
    SqliteCoroScheduler* pool = SqliteExtCoroPool<ExtensionTag>::get();
    if (!pool) {
        pool = SqliteExtCoroPool<ExtensionTag>::acquire();
        if (!pool) return false;
    }
    return sqlite_coro_spawn(pool, sqlite_forward<F>(f), stack_size);
}

// ============================================================================
// 3. TAGGED C++ EXTENSION POOL REGISTRY WRAPPER
// ============================================================================

/**
 * @class SqliteTaggedCoroPool
 * @brief C++ wrapper around the collision-proof static tag registry (`sqlite3_coro_ext_pool.h`).
 */
class SqliteTaggedCoroPool {
public:
    static inline sqlite3_coro_pool_t* acquire(const void* tag, int num_workers = 4) {
        return sqlite3_coro_ext_pool_acquire(tag, num_workers);
    }

    static inline void release(const void* tag) {
        sqlite3_coro_ext_pool_release(tag);
    }

    static inline void wait_all(const void* tag) {
        sqlite3_coro_ext_pool_wait(tag);
    }

    static inline int ref_count(const void* tag) {
        return sqlite3_coro_ext_pool_ref_count(tag);
    }

    static inline void shutdown_all() {
        sqlite3_coro_ext_pool_shutdown_all();
    }
};

#endif /* SQLITE3_CORO_EXT_POOL_HPP */
