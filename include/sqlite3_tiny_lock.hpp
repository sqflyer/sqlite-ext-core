#ifndef SQLITE3_TINY_LOCK_HPP
#define SQLITE3_TINY_LOCK_HPP

#include "sqlite3_lock_base.hpp"
#include "sqlite3_tiny_lock.h"

/**
 * @brief C++ Wrapper for sqlite3_tiny_lock
 * Provides standard C++ object-oriented methods (lock, unlock, try_lock) 
 * over the highly optimized 4-byte C struct.
 */
class SqliteTinyLock : public SqliteLockBase {
    sqlite3_tiny_lock m_lock;

public:
    SqliteTinyLock() noexcept {
        sqlite3_tiny_lock_init(&m_lock);
    }

    ~SqliteTinyLock() = default;

    void lock() noexcept {
        sqlite3_tiny_lock_lock(&m_lock);
    }

    bool try_lock() noexcept {
        return sqlite3_tiny_lock_try_lock(&m_lock) != 0;
    }

    void unlock() noexcept {
        sqlite3_tiny_lock_unlock(&m_lock);
    }

    void lock_read() noexcept { lock(); }
    void unlock_read() noexcept { unlock(); }
    void lock_write() noexcept { lock(); }
    void unlock_write() noexcept { unlock(); }

    // Allow access to the underlying C-struct if needed for C-APIs
    sqlite3_tiny_lock* native_handle() noexcept {
        return &m_lock;
    }
};

/**
 * @brief RAII Guard for SqliteTinyLock
 */
class SqliteTinyLockGuard : public SqliteLockGuard<SqliteTinyLock> {
public:
    explicit SqliteTinyLockGuard(SqliteTinyLock& lock) noexcept 
        : SqliteLockGuard<SqliteTinyLock>(lock) {}
};

#endif // SQLITE3_TINY_LOCK_HPP
