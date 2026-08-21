#ifndef SQLITE3_MUTEX_LOCK_HPP
#define SQLITE3_MUTEX_LOCK_HPP

#include <sqlite3.h>
#include <sqlite3ext.h>

/**
 * @brief An owning C++ wrapper around a dynamic sqlite3_mutex.
 * 
 * Allocates a fast mutex on construction and frees it on destruction.
 * Provides standard C++ lock(), unlock(), and try_lock() methods.
 */
class SqliteMutex {
private:
    sqlite3_mutex* m_mutex;

public:
    /**
     * @brief Constructs the mutex object.
     * Allocates a dynamic `SQLITE_MUTEX_FAST` from the SQLite engine.
     * Note: In single-threaded SQLite builds, this may allocate a null pointer, 
     * which is safely handled by all lock methods.
     */
    SqliteMutex() noexcept {
        m_mutex = sqlite3_mutex_alloc(SQLITE_MUTEX_FAST);
    }

    /**
     * @brief Destructs the mutex object.
     * Safely frees the underlying `sqlite3_mutex` back to the SQLite engine.
     */
    ~SqliteMutex() {
        if (m_mutex) {
            sqlite3_mutex_free(m_mutex);
        }
    }

    // Locks cannot be copied or moved.
    SqliteMutex(const SqliteMutex&) = delete;
    SqliteMutex& operator=(const SqliteMutex&) = delete;

    /**
     * @brief Blocks the current thread until the mutex is acquired.
     * Safely becomes a no-op if the underlying mutex is null.
     */
    void lock() noexcept {
        if (m_mutex) {
            sqlite3_mutex_enter(m_mutex);
        }
    }

    /**
     * @brief Attempts to acquire the mutex without blocking.
     * @return `true` if acquired successfully (or if the mutex is null in single-threaded mode).
     *         `false` if currently locked by another thread.
     */
    bool try_lock() noexcept {
        if (m_mutex) {
            return sqlite3_mutex_try(m_mutex) == SQLITE_OK;
        }
        // In SQLite single-threaded mode, mutexes are null.
        // Therefore, locking trivially "succeeds" instantly.
        return true; 
    }

    /**
     * @brief Releases the mutex, allowing other threads to acquire it.
     */
    void unlock() noexcept {
        if (m_mutex) {
            sqlite3_mutex_leave(m_mutex);
        }
    }

    /**
     * @brief Provides access to the raw underlying SQLite mutex pointer.
     * Useful for passing into standard SQLite C-APIs.
     * @return sqlite3_mutex* (may be null)
     */
    sqlite3_mutex* native_handle() const noexcept {
        return m_mutex;
    }
};

/**
 * @brief Exception-safe RAII Guard for standard SQLite Mutexes.
 */
class SqliteMutexGuard {
private:
    sqlite3_mutex* m_mutex;

public:
    /**
     * @brief Constructs the guard from a raw sqlite3_mutex pointer.
     * @param mutex A pointer to the sqlite3_mutex. If null, operations are skipped.
     */
    explicit SqliteMutexGuard(sqlite3_mutex* mutex) noexcept : m_mutex(mutex) {
        if (m_mutex) {
            sqlite3_mutex_enter(m_mutex);
        }
    }

    /**
     * @brief Constructs the guard from a SqliteMutex C++ wrapper.
     */
    explicit SqliteMutexGuard(SqliteMutex& mutex_obj) noexcept : SqliteMutexGuard(mutex_obj.native_handle()) {}

    /**
     * @brief Destructs the guard and leaves the mutex.
     */
    ~SqliteMutexGuard() {
        if (m_mutex) {
            sqlite3_mutex_leave(m_mutex);
        }
    }

    // Locks cannot be copied or moved safely.
    SqliteMutexGuard(const SqliteMutexGuard&) = delete;
    SqliteMutexGuard& operator=(const SqliteMutexGuard&) = delete;
};

#endif // SQLITE3_MUTEX_LOCK_HPP
