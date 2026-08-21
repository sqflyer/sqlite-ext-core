#ifndef SQLITE3_RW_LOCK_H
#define SQLITE3_RW_LOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sqlite3.h>
#include "sqlite3_tiny_lock.h"

// =================================================================================
// CROSS-PLATFORM READ/WRITE LOCK
// =================================================================================
// To support high-concurrency without degrading read performance, we map this 
// abstraction to native OS Read/Write locks. These allow concurrent readers 
// but exclusive writers.

#ifdef _WIN32
#include <windows.h>
typedef SRWLOCK sqlite3_rw_lock;
#define sqlite3_rw_lock_init(lock_ptr) InitializeSRWLock(lock_ptr)
#define sqlite3_rw_lock_read_acquire(lock_ptr) AcquireSRWLockShared(lock_ptr)
#define sqlite3_rw_lock_read_release(lock_ptr) ReleaseSRWLockShared(lock_ptr)
#define sqlite3_rw_lock_write_acquire(lock_ptr) AcquireSRWLockExclusive(lock_ptr)
#define sqlite3_rw_lock_write_release(lock_ptr) ReleaseSRWLockExclusive(lock_ptr)
#define sqlite3_rw_lock_destroy(lock_ptr) /* SRWLOCK does not require explicit destruction */

#elif defined(__wasm__) || defined(__EMSCRIPTEN__)
/* WASM environments lack pthread read/write locks. We use sqlite3_tiny_lock as the fallback 
 * because it natively compiles down to `memory.atomic.wait32`, allowing it to completely 
 * pause execution and put the thread to sleep (0% CPU) without requiring a dynamic memory allocation. */
typedef sqlite3_tiny_lock sqlite3_rw_lock;
#define sqlite3_rw_lock_init(lock_ptr) sqlite3_tiny_lock_init(lock_ptr)
#define sqlite3_rw_lock_read_acquire(lock_ptr) sqlite3_tiny_lock_lock(lock_ptr)
#define sqlite3_rw_lock_read_release(lock_ptr) sqlite3_tiny_lock_unlock(lock_ptr)
#define sqlite3_rw_lock_write_acquire(lock_ptr) sqlite3_tiny_lock_lock(lock_ptr)
#define sqlite3_rw_lock_write_release(lock_ptr) sqlite3_tiny_lock_unlock(lock_ptr)
#define sqlite3_rw_lock_destroy(lock_ptr) /* TinyLock does not require explicit destruction */

#else
#include <pthread.h>
typedef pthread_rwlock_t sqlite3_rw_lock;
#define sqlite3_rw_lock_init(lock_ptr) pthread_rwlock_init((lock_ptr), NULL)
#define sqlite3_rw_lock_read_acquire(lock_ptr) pthread_rwlock_rdlock(lock_ptr)
#define sqlite3_rw_lock_read_release(lock_ptr) pthread_rwlock_unlock(lock_ptr)
#define sqlite3_rw_lock_write_acquire(lock_ptr) pthread_rwlock_wrlock(lock_ptr)
#define sqlite3_rw_lock_write_release(lock_ptr) pthread_rwlock_unlock(lock_ptr)
#define sqlite3_rw_lock_destroy(lock_ptr) pthread_rwlock_destroy(lock_ptr)
#endif

#ifdef __cplusplus
}
#endif

#endif // SQLITE3_RW_LOCK_H
