#ifndef SQLITE3_THREAD_HPP
#define SQLITE3_THREAD_HPP

/**
 * @file sqlite3_thread.hpp
 * @brief Zero-dependency C++11 threading, condition variable, and native mutex subsystem.
 *
 * ## Architectural Overview
 * Mimics `std::thread`, `std::mutex`, `std::unique_lock`, and `std::condition_variable` while
 * remaining strictly compliant with freestanding, `-nostdlib++`, `-fno-exceptions`, and `-fno-rtti`
 * build environments:
 *
 * 1. **Zero Runtime Bloat**:
 *    - Bypasses `<thread>`, `<mutex>`, `<condition_variable>`, and `<functional>` standard headers.
 *    - Memory allocations for capturing lambdas route strictly through SQLite's allocator (`sqlite_new` / `sqlite_delete`).
 *
 * 2. **Native OS Synchronization**:
 *    - `SqliteThreadMutex` wraps native OS recursive primitives (`CRITICAL_SECTION` on Windows, `pthread_mutex_t` on POSIX).
 *    - `SqliteConditionVariable` maps directly to `CONDITION_VARIABLE` (Windows) and `pthread_cond_t` (POSIX) for
 *      true kernel-managed sleeping with 0% CPU consumption.
 *
 * 3. **Ergonomic Predicate Waits**:
 *    - Supports `wait(guard, predicate)` and `wait_for(guard, timeout_ms, predicate)` loop helpers guarding against
 *      spurious wakeups.
 *
 * 4. **Move Semantics & Safe RAII Teardown**:
 *    - `SqliteThread` is move-only and automatically detaches joinable handles on destruction to prevent resource leaks.
 */

#include "sqlite3_thread.h"
#include "../sqlite3_lock_base.hpp"
#include "../sqlite3_allocator.hpp"
#include <sqlite3.h>

/**
 * @class SqliteThreadMutex
 * @brief Zero-overhead native OS mutex wrapper for condition variable synchronization.
 *
 * Inherits from `SqliteLockBase` for compatibility with RAII guard wrappers.
 *
 * @code
 * SqliteThreadMutex mtx;
 * {
 *     SqliteThreadMutexGuard guard(mtx);
 *     // Critical section protected
 * }
 * @endcode
 */
class SqliteThreadMutex : public SqliteLockBase {
private:
    sqlite3_thread_mutex_t m_mutex;  /**< Underlying OS native mutex handle. */

public:
    /**
     * @brief Constructs and initializes the native OS mutex.
     */
    inline SqliteThreadMutex() {
        sqlite3_thread_mutex_init(&m_mutex);
    }

    /**
     * @brief Destroys the native OS mutex and releases system resources.
     */
    inline ~SqliteThreadMutex() {
        sqlite3_thread_mutex_destroy(&m_mutex);
    }

    // Non-copyable
    SqliteThreadMutex(const SqliteThreadMutex&) = delete;
    SqliteThreadMutex& operator=(const SqliteThreadMutex&) = delete;

    /**
     * @brief Acquires exclusive ownership of the mutex, blocking until acquired.
     */
    inline void lock() {
        sqlite3_thread_mutex_lock(&m_mutex);
    }

    /**
     * @brief Releases ownership of the mutex.
     */
    inline void unlock() {
        sqlite3_thread_mutex_unlock(&m_mutex);
    }

    /**
     * @brief Returns the underlying native OS mutex handle.
     * @return Pointer to the native `sqlite3_thread_mutex_t`.
     */
    inline sqlite3_thread_mutex_t* native_handle() {
        return &m_mutex;
    }
};

/**
 * @class SqliteThreadMutexGuard
 * @brief Scoped RAII guard for acquiring and releasing a `SqliteThreadMutex` automatically.
 */
class SqliteThreadMutexGuard : public SqliteGuardBase {
private:
    SqliteThreadMutex& m_mutex;  /**< Reference to the locked mutex. */

public:
    /**
     * @brief Locks the associated mutex upon construction.
     * @param m The `SqliteThreadMutex` reference to lock.
     */
    explicit inline SqliteThreadMutexGuard(SqliteThreadMutex& m) : m_mutex(m) {
        m_mutex.lock();
    }

    /**
     * @brief Unlocks the associated mutex upon destruction.
     */
    inline ~SqliteThreadMutexGuard() {
        m_mutex.unlock();
    }

    // Non-copyable
    SqliteThreadMutexGuard(const SqliteThreadMutexGuard&) = delete;
    SqliteThreadMutexGuard& operator=(const SqliteThreadMutexGuard&) = delete;

    /**
     * @brief Returns reference to the guarded mutex.
     * @return Reference to the underlying `SqliteThreadMutex`.
     */
    inline SqliteThreadMutex& mutex() {
        return m_mutex;
    }
};

/**
 * @class SqliteConditionVariable
 * @brief Freestanding C++11 condition variable wrapping native OS synchronization primitives.
 *
 * Provides thread notification mechanisms (`notify_one`, `notify_all`) and predicate wait loops.
 *
 * @code
 * SqliteThreadMutex mtx;
 * SqliteConditionVariable cv;
 * bool ready = false;
 *
 * // Consumer
 * {
 *     SqliteThreadMutexGuard guard(mtx);
 *     cv.wait(guard, [&ready]() { return ready; });
 * }
 *
 * // Producer
 * {
 *     SqliteThreadMutexGuard guard(mtx);
 *     ready = true;
 *     cv.notify_one();
 * }
 * @endcode
 */
class SqliteConditionVariable {
private:
    sqlite3_cond_t m_cond;  /**< Underlying OS native condition variable handle. */

public:
    /**
     * @brief Constructs and initializes the native condition variable.
     */
    inline SqliteConditionVariable() {
        sqlite3_cond_init(&m_cond);
    }

    /**
     * @brief Destroys the condition variable.
     */
    inline ~SqliteConditionVariable() {
        sqlite3_cond_destroy(&m_cond);
    }

    // Non-copyable
    SqliteConditionVariable(const SqliteConditionVariable&) = delete;
    SqliteConditionVariable& operator=(const SqliteConditionVariable&) = delete;

    /**
     * @brief Wakes up at least one waiting thread.
     */
    inline void notify_one() {
        sqlite3_cond_signal(&m_cond);
    }

    /**
     * @brief Wakes up all waiting threads simultaneously.
     */
    inline void notify_all() {
        sqlite3_cond_broadcast(&m_cond);
    }

    /**
     * @brief Atomically unlocks the mutex and blocks indefinitely until notified.
     * @param mutex The locked `SqliteThreadMutex` instance.
     */
    inline void wait(SqliteThreadMutex& mutex) {
        sqlite3_cond_wait(&m_cond, mutex.native_handle());
    }

    /**
     * @brief Atomically unlocks the mutex guard and blocks indefinitely until notified.
     * @param guard The locked `SqliteThreadMutexGuard` instance.
     */
    inline void wait(SqliteThreadMutexGuard& guard) {
        sqlite3_cond_wait(&m_cond, guard.mutex().native_handle());
    }

    /**
     * @brief Atomically blocks until notified or timeout expires.
     * @param guard The locked `SqliteThreadMutexGuard` instance.
     * @param timeout_ms Maximum time to wait in milliseconds.
     * @return True if signaled before timeout, false if timed out.
     */
    inline bool wait_for(SqliteThreadMutexGuard& guard, unsigned int timeout_ms) {
        int rc = sqlite3_cond_timedwait(&m_cond, guard.mutex().native_handle(), timeout_ms);
        return (rc == 0);
    }

    /**
     * @brief Blocks until notified and the given predicate evaluates to true.
     *
     * Automatically loops on predicate evaluation to protect against spurious wakeups.
     *
     * @tparam Predicate Callable returning boolean.
     * @param guard The locked `SqliteThreadMutexGuard` instance.
     * @param pred Predicate function or lambda to check.
     */
    template <typename Predicate>
    inline void wait(SqliteThreadMutexGuard& guard, Predicate pred) {
        while (!pred()) {
            wait(guard);
        }
    }

    /**
     * @brief Blocks until notified and predicate is true, or timeout expires.
     *
     * @tparam Predicate Callable returning boolean.
     * @param guard The locked `SqliteThreadMutexGuard` instance.
     * @param timeout_ms Maximum time to wait in milliseconds.
     * @param pred Predicate function or lambda to check.
     * @return True if predicate evaluated to true, false if timed out.
     */
    template <typename Predicate>
    inline bool wait_for(SqliteThreadMutexGuard& guard, unsigned int timeout_ms, Predicate pred) {
        uint64_t start_ms = sqlite3_time_ms();
        while (!pred()) {
            uint64_t elapsed = sqlite3_time_ms() - start_ms;
            if (elapsed >= timeout_ms) {
                return pred();
            }
            if (!wait_for(guard, static_cast<unsigned int>(timeout_ms - elapsed))) {
                return pred();
            }
        }
        return true;
    }
};

/**
 * @class SqliteThread
 * @brief Zero-dependency C++11 thread wrapper mimicking `std::thread` without standard library dependencies.
 *
 * Supports move semantics, raw function pointers, capturing lambdas, and member function invocations.
 *
 * @code
 * // Spawning a capturing lambda
 * int val = 10;
 * SqliteThread t([&val]() {
 *     val += 20;
 * });
 * t.join();
 * assert(val == 30);
 * @endcode
 */
class SqliteThread {
private:
    sqlite3_thread_t m_thread;    /**< Underlying cross-platform thread handle. */
    bool             m_joinable;  /**< Tracks whether thread is active and joinable. */

    /**
     * @struct CallableHolderBase
     * @brief Non-virtual type-erased closure holder for thread execution.
     */
    struct CallableHolderBase {
        void (*invoke_fn)(CallableHolderBase*);   /**< Invocation function pointer. */
        void (*destroy_fn)(CallableHolderBase*);  /**< Destruction function pointer. */
    };

    /**
     * @struct CallableHolder
     * @brief Templated closure container storing user-supplied functors and lambdas.
     */
    template <typename F>
    struct CallableHolder : public CallableHolderBase {
        F func;
        CallableHolder(F&& f) : func(static_cast<F&&>(f)) {
            invoke_fn = &invoke_impl;
            destroy_fn = &destroy_impl;
        }
        static void invoke_impl(CallableHolderBase* self) {
            static_cast<CallableHolder<F>*>(self)->func();
        }
        static void destroy_impl(CallableHolderBase* self) {
            sqlite_delete(static_cast<CallableHolder<F>*>(self));
        }
    };

    static void* callable_trampoline(void* arg) {
        CallableHolderBase* holder = static_cast<CallableHolderBase*>(arg);
        if (holder) {
            holder->invoke_fn(holder);
            holder->destroy_fn(holder);
        }
        return nullptr;
    }

    static void* fn_trampoline(void* arg) {
        typedef void (*RawFn)();
        RawFn fn = reinterpret_cast<RawFn>(arg);
        if (fn) fn();
        return nullptr;
    }

public:
    /**
     * @brief Constructs an empty, non-joinable thread instance.
     */
    inline SqliteThread() : m_joinable(false) {
#if defined(_WIN32) || defined(_WIN64)
        m_thread.handle = NULL;
        m_thread.func = NULL;
        m_thread.arg = NULL;
        m_thread.retval = NULL;
        m_thread.is_detached = 0;
#else
        m_thread = (pthread_t)0;
#endif
    }

    /**
     * @brief Constructs and launches a thread from a parameterless function pointer.
     * @param fn Function pointer of type `void (*)()`.
     */
    inline explicit SqliteThread(void (*fn)()) : m_joinable(false) {
        int rc = sqlite3_thread_create(&m_thread, fn_trampoline, reinterpret_cast<void*>(fn));
        m_joinable = (rc == 0);
    }

    /**
     * @brief Constructs and launches a thread from a function pointer with user data.
     * @tparam Arg Type of the argument structure.
     * @param func Function pointer of type `void* (*)(Arg*)`.
     * @param arg Pointer to pass as argument.
     */
    template <typename Arg>
    inline SqliteThread(void* (*func)(Arg*), Arg* arg) : m_joinable(false) {
        typedef void* (*RawThreadFunc)(void*);
        int rc = sqlite3_thread_create(&m_thread, reinterpret_cast<RawThreadFunc>(func), static_cast<void*>(arg));
        m_joinable = (rc == 0);
    }

    /**
     * @brief Constructs and launches a thread from a callable object or capturing lambda.
     * @tparam Callable Type of the callable closure.
     * @param callable Lambda or functor to execute in the background thread.
     */
    template <typename Callable>
    inline explicit SqliteThread(Callable callable) : m_joinable(false) {
        CallableHolder<Callable>* holder = sqlite_new<CallableHolder<Callable>>(sqlite_forward<Callable>(callable));
        if (holder) {
            int rc = sqlite3_thread_create(&m_thread, callable_trampoline, holder);
            m_joinable = (rc == 0);
            if (!m_joinable) {
                sqlite_delete(holder);
            }
        }
    }

    /**
     * @brief Destructor. Automatically detaches handle if still joinable to prevent resource leaks.
     */
    inline ~SqliteThread() {
        if (m_joinable) {
            detach();
        }
    }

    // Move-only semantics (non-copyable)
    SqliteThread(const SqliteThread&) = delete;
    SqliteThread& operator=(const SqliteThread&) = delete;

    /**
     * @brief Move constructor transfers thread execution context and ownership.
     * @param other The thread instance to move from.
     */
    inline SqliteThread(SqliteThread&& other) noexcept : m_thread(other.m_thread), m_joinable(other.m_joinable) {
        other.m_joinable = false;
#if defined(_WIN32) || defined(_WIN64)
        other.m_thread.handle = NULL;
#else
        other.m_thread = (pthread_t)0;
#endif
    }

    /**
     * @brief Move assignment operator transfers thread execution context.
     * @param other The thread instance to move from.
     * @return Reference to this instance.
     */
    inline SqliteThread& operator=(SqliteThread&& other) noexcept {
        if (this != &other) {
            if (m_joinable) {
                detach();
            }
            m_thread = other.m_thread;
            m_joinable = other.m_joinable;
            other.m_joinable = false;
#if defined(_WIN32) || defined(_WIN64)
            other.m_thread.handle = NULL;
#else
            other.m_thread = (pthread_t)0;
#endif
        }
        return *this;
    }

    /**
     * @brief Returns true if the thread represents an active execution context.
     * @return True if joinable.
     */
    inline bool joinable() const {
        return m_joinable;
    }

    /**
     * @brief Blocks calling thread until this thread completes execution.
     * @param retval Optional output pointer to store the thread exit status.
     * @return 0 on success, -1 on failure.
     */
    inline int join(void** retval = nullptr) {
        if (!m_joinable) return -1;
        int rc = sqlite3_thread_join(&m_thread, retval);
        m_joinable = false;
        return rc;
    }

    /**
     * @brief Detaches the thread, allowing it to continue execution independently.
     * @return 0 on success, -1 on failure.
     */
    inline int detach() {
        if (!m_joinable) return -1;
        int rc = sqlite3_thread_detach(&m_thread);
        m_joinable = false;
        return rc;
    }

    /**
     * @brief Yields the current thread execution slot to the OS scheduler.
     */
    static inline void yield() {
        sqlite3_thread_yield();
    }

    /**
     * @brief Pauses execution of the calling thread for the given milliseconds.
     * @param ms Duration in milliseconds.
     */
    static inline void sleep_for_ms(unsigned int ms) {
        sqlite3_time_sleep_ms(ms);
    }

    /**
     * @brief Pauses execution of the calling thread for the given microseconds.
     * @param us Duration in microseconds.
     */
    static inline void sleep_for_us(unsigned int us) {
        sqlite3_time_sleep_us(us);
    }
};

#endif // SQLITE3_THREAD_HPP
