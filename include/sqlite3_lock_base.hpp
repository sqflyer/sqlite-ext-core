#ifndef SQLITE3_LOCK_BASE_HPP
#define SQLITE3_LOCK_BASE_HPP

/**
 * @brief Non-copyable, non-movable base class for all SQLite lock primitives.
 * 
 * Enforces address stability and prevents accidental copying or moving of 
 * synchronization primitives across threads or scopes.
 */
class SqliteLockBase {
public:
    SqliteLockBase() noexcept = default;
    ~SqliteLockBase() = default;

    // Locks are address-bound and must not be copied or moved
    SqliteLockBase(const SqliteLockBase&) = delete;
    SqliteLockBase& operator=(const SqliteLockBase&) = delete;
    SqliteLockBase(SqliteLockBase&&) = delete;
    SqliteLockBase& operator=(SqliteLockBase&&) = delete;
};

/**
 * @brief Non-copyable, non-movable base class for all SQLite RAII guard primitives.
 */
class SqliteGuardBase {
public:
    SqliteGuardBase() noexcept = default;
    ~SqliteGuardBase() = default;

    // Guards must not be copied or moved
    SqliteGuardBase(const SqliteGuardBase&) = delete;
    SqliteGuardBase& operator=(const SqliteGuardBase&) = delete;
    SqliteGuardBase(SqliteGuardBase&&) = delete;
    SqliteGuardBase& operator=(SqliteGuardBase&&) = delete;
};

/**
 * @brief Generic RAII Guard for exclusive locking (lock / unlock).
 * Supports any lock type implementing lock() and unlock() (SqliteMutex, SqliteTinyLock, SqliteRwLock).
 */
template <typename Lockable>
class SqliteLockGuard : public SqliteGuardBase {
private:
    Lockable& m_lock;

public:
    explicit SqliteLockGuard(Lockable& lock) noexcept : m_lock(lock) {
        m_lock.lock();
    }

    ~SqliteLockGuard() noexcept {
        m_lock.unlock();
    }
};

/**
 * @brief Generic RAII Guard for shared read locking (lock_read / unlock_read).
 */
template <typename Lockable>
class SqliteBasicReadGuard : public SqliteGuardBase {
private:
    Lockable& m_lock;

public:
    explicit SqliteBasicReadGuard(Lockable& lock) noexcept : m_lock(lock) {
        m_lock.lock_read();
    }

    ~SqliteBasicReadGuard() noexcept {
        m_lock.unlock_read();
    }
};

/**
 * @brief Generic RAII Guard for exclusive write locking (lock_write / unlock_write).
 */
template <typename Lockable>
class SqliteBasicWriteGuard : public SqliteGuardBase {
private:
    Lockable& m_lock;

public:
    explicit SqliteBasicWriteGuard(Lockable& lock) noexcept : m_lock(lock) {
        m_lock.lock_write();
    }

    ~SqliteBasicWriteGuard() noexcept {
        m_lock.unlock_write();
    }
};

#endif // SQLITE3_LOCK_BASE_HPP
