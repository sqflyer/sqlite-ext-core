#ifndef SQLITE3_TINY_LOCK_HPP
#define SQLITE3_TINY_LOCK_HPP

#include "sqlite3_tiny_lock.h"

/**
 * @brief C++ Wrapper for sqlite3_tiny_lock
 * Provides standard C++ object-oriented methods (lock, unlock, try_lock) 
 * over the highly optimized 4-byte C struct.
 */
class SqliteTinyLock {
    sqlite3_tiny_lock m_lock;

public:
    SqliteTinyLock() noexcept {
        sqlite3_tiny_lock_init(&m_lock);
    }

    // Non-copyable and non-movable (locks shouldn't be copied)
    SqliteTinyLock(const SqliteTinyLock&) = delete;
    SqliteTinyLock& operator=(const SqliteTinyLock&) = delete;

    void lock() noexcept {
        sqlite3_tiny_lock_lock(&m_lock);
    }

    bool try_lock() noexcept {
        return sqlite3_tiny_lock_try_lock(&m_lock) != 0;
    }

    void unlock() noexcept {
        sqlite3_tiny_lock_unlock(&m_lock);
    }

    // Allow access to the underlying C-struct if needed for C-APIs
    sqlite3_tiny_lock* native_handle() noexcept {
        return &m_lock;
    }
};

/**
 * @brief RAII Guard for SqliteTinyLock
 * Automatically acquires the lock upon construction and releases it upon destruction.
 * Guarantees exception-safe unlocking (even with -fno-exceptions, it handles early returns gracefully).
 */
class SqliteTinyLockGuard {
    SqliteTinyLock& m_lock;

public:
    explicit SqliteTinyLockGuard(SqliteTinyLock& lock) noexcept : m_lock(lock) {
        m_lock.lock();
    }

    ~SqliteTinyLockGuard() noexcept {
        m_lock.unlock();
    }

    // Non-copyable
    SqliteTinyLockGuard(const SqliteTinyLockGuard&) = delete;
    SqliteTinyLockGuard& operator=(const SqliteTinyLockGuard&) = delete;
};

#endif // SQLITE3_TINY_LOCK_HPP
