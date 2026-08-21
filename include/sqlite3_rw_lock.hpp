#ifndef SQLITE3_RW_LOCK_HPP
#define SQLITE3_RW_LOCK_HPP

#include <sqlite3.h>

#include "sqlite3_rw_lock.h"
// C++ RAII WRAPPERS
// =================================================================================

/**
 * @brief An owning C++ wrapper around a cross-platform Read/Write lock.
 */
class SqliteRwLock {
private:
    sqlite3_rw_lock m_lock;

public:
    SqliteRwLock() noexcept {
        sqlite3_rw_lock_init(&m_lock);
    }

    ~SqliteRwLock() {
        sqlite3_rw_lock_destroy(&m_lock);
    }

    // Locks cannot be copied or moved.
    SqliteRwLock(const SqliteRwLock&) = delete;
    SqliteRwLock& operator=(const SqliteRwLock&) = delete;

    void lock_read() noexcept { sqlite3_rw_lock_read_acquire(&m_lock); }
    void unlock_read() noexcept { sqlite3_rw_lock_read_release(&m_lock); }
    
    void lock_write() noexcept { sqlite3_rw_lock_write_acquire(&m_lock); }
    void unlock_write() noexcept { sqlite3_rw_lock_write_release(&m_lock); }

    sqlite3_rw_lock* native_handle() noexcept {
        return &m_lock;
    }
};

/**
 * @brief Exception-safe RAII Guard for SHARED (Read) access.
 */
class SqliteReadGuard {
private:
    SqliteRwLock* m_lock;

public:
    explicit SqliteReadGuard(SqliteRwLock& lock) noexcept : m_lock(&lock) {
        if (m_lock) {
            m_lock->lock_read();
        }
    }

    ~SqliteReadGuard() {
        if (m_lock) {
            m_lock->unlock_read();
        }
    }

    // Guards cannot be copied or moved
    SqliteReadGuard(const SqliteReadGuard&) = delete;
    SqliteReadGuard& operator=(const SqliteReadGuard&) = delete;
};

/**
 * @brief Exception-safe RAII Guard for EXCLUSIVE (Write) access.
 */
class SqliteWriteGuard {
private:
    SqliteRwLock* m_lock;

public:
    explicit SqliteWriteGuard(SqliteRwLock& lock) noexcept : m_lock(&lock) {
        if (m_lock) {
            m_lock->lock_write();
        }
    }

    ~SqliteWriteGuard() {
        if (m_lock) {
            m_lock->unlock_write();
        }
    }

    // Guards cannot be copied or moved
    SqliteWriteGuard(const SqliteWriteGuard&) = delete;
    SqliteWriteGuard& operator=(const SqliteWriteGuard&) = delete;
};

#endif // SQLITE3_RW_LOCK_HPP
