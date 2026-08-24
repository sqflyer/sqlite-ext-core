#ifndef SQLITE3_RW_LOCK_HPP
#define SQLITE3_RW_LOCK_HPP

#include <sqlite3.h>
#include "sqlite3_lock_base.hpp"
#include "sqlite3_rw_lock.h"

// =================================================================================
// C++ RAII WRAPPERS
// =================================================================================

/**
 * @brief An owning C++ wrapper around a cross-platform Read/Write lock.
 */
class SqliteRwLock : public SqliteLockBase {
private:
    sqlite3_rw_lock m_lock;

public:
    SqliteRwLock() noexcept {
        sqlite3_rw_lock_init(&m_lock);
    }

    ~SqliteRwLock() {
        sqlite3_rw_lock_destroy(&m_lock);
    }

    void lock() noexcept { lock_write(); }
    void unlock() noexcept { unlock_write(); }

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
class SqliteReadGuard : public SqliteBasicReadGuard<SqliteRwLock> {
public:
    explicit SqliteReadGuard(SqliteRwLock& lock) noexcept 
        : SqliteBasicReadGuard<SqliteRwLock>(lock) {}
};

/**
 * @brief Exception-safe RAII Guard for EXCLUSIVE (Write) access.
 */
class SqliteWriteGuard : public SqliteBasicWriteGuard<SqliteRwLock> {
public:
    explicit SqliteWriteGuard(SqliteRwLock& lock) noexcept 
        : SqliteBasicWriteGuard<SqliteRwLock>(lock) {}
};

#endif // SQLITE3_RW_LOCK_HPP
