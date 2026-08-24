#ifndef SQLITE3_MUTEX_LOCK_H
#define SQLITE3_MUTEX_LOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sqlite3.h>
#include <stddef.h>

// ============================================================================
// SQLITE NATIVE MUTEX WRAPPER (C INTERFACE)
// ============================================================================

typedef struct {
    sqlite3_mutex* handle;
} sqlite3_mutex_lock;

/**
 * @brief Initializes a dynamic SQLite fast mutex.
 */
static inline void sqlite3_mutex_lock_init(sqlite3_mutex_lock* lock) {
    if (lock) {
        lock->handle = sqlite3_mutex_alloc(SQLITE_MUTEX_FAST);
    }
}

/**
 * @brief Initializes a dynamic SQLite mutex of a specific type (e.g. SQLITE_MUTEX_RECURSIVE).
 */
static inline void sqlite3_mutex_lock_init_type(sqlite3_mutex_lock* lock, int mutex_type) {
    if (lock) {
        lock->handle = sqlite3_mutex_alloc(mutex_type);
    }
}

/**
 * @brief Frees the underlying sqlite3_mutex.
 */
static inline void sqlite3_mutex_lock_destroy(sqlite3_mutex_lock* lock) {
    if (lock && lock->handle) {
        sqlite3_mutex_free(lock->handle);
        lock->handle = NULL;
    }
}

/**
 * @brief Acquires the mutex exclusively.
 */
static inline void sqlite3_mutex_lock_lock(sqlite3_mutex_lock* lock) {
    if (lock && lock->handle) {
        sqlite3_mutex_enter(lock->handle);
    }
}

/**
 * @brief Attempts to acquire the mutex without blocking.
 */
static inline int sqlite3_mutex_lock_try_lock(sqlite3_mutex_lock* lock) {
    if (lock && lock->handle) {
        return sqlite3_mutex_try(lock->handle) == SQLITE_OK;
    }
    return 1;
}

/**
 * @brief Releases the mutex.
 */
static inline void sqlite3_mutex_lock_unlock(sqlite3_mutex_lock* lock) {
    if (lock && lock->handle) {
        sqlite3_mutex_leave(lock->handle);
    }
}

// Uniform locking adapters for generic state manager integration
#define sqlite3_mutex_lock_read_acquire(lock_ptr)   sqlite3_mutex_lock_lock(lock_ptr)
#define sqlite3_mutex_lock_read_release(lock_ptr)   sqlite3_mutex_lock_unlock(lock_ptr)
#define sqlite3_mutex_lock_write_acquire(lock_ptr)  sqlite3_mutex_lock_lock(lock_ptr)
#define sqlite3_mutex_lock_write_release(lock_ptr)  sqlite3_mutex_lock_unlock(lock_ptr)

#ifdef __cplusplus
}
#endif

#endif // SQLITE3_MUTEX_LOCK_H
