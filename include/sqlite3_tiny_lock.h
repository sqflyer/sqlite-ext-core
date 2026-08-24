#ifndef SQLITE3_TINY_LOCK_H
#define SQLITE3_TINY_LOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sqlite3_atomic.h"

// ============================================================================
// CPU RELAXATION (SPINLOCK YIELDING)
// ============================================================================
// Pauses the CPU to prevent 100% core starvation during a spin loop.

#if defined(__wasm__)
    #define SQLITE_CPU_RELAX(ptr) __builtin_wasm_memory_atomic_wait32((int*)(ptr), 1, -1)
    #define SQLITE_CPU_NOTIFY(ptr) __builtin_wasm_memory_atomic_notify((int*)(ptr), 1)
#elif defined(_MSC_VER)
    #include <intrin.h>
    #define SQLITE_CPU_RELAX(ptr) _mm_pause()
    #define SQLITE_CPU_NOTIFY(ptr)
#else
    static inline void _sqlite_gcc_cpu_relax() {
    #if defined(__i386__) || defined(__x86_64__)
        __builtin_ia32_pause(); 
    #elif defined(__aarch64__) || defined(__arm__)
        __asm__ volatile("yield" ::: "memory");
    #elif defined(__riscv)
        __asm__ volatile("pause" ::: "memory");
    #else
        __asm__ volatile("" ::: "memory");
    #endif
    }
    #define SQLITE_CPU_RELAX(ptr) _sqlite_gcc_cpu_relax()
    #define SQLITE_CPU_NOTIFY(ptr)
#endif

// ============================================================================
// TINY LOCK INTERFACE
// ============================================================================

/**
 * @brief sqlite3_tiny_lock: A 4-byte, zero-dependency, cross-platform hybrid lock.
 * 
 * - Native Architecture: Acts as a blistering-fast, CPU-yielding Spinlock.
 * - WebAssembly: Acts as a true 0% CPU sleeping Mutex.
 */
#if defined(__wasm__)
    typedef int sqlite3_tiny_lock_state_t;
    #define SQLITE_TINY_LOCK_CAS_WEAK(ptr, exp, des)   sqlite_atomic_cas_weak_32(ptr, exp, des)
    #define SQLITE_TINY_LOCK_CAS_STRONG(ptr, exp, des) sqlite_atomic_cas_strong_32(ptr, exp, des)
    #define SQLITE_TINY_LOCK_STORE(ptr, val)           sqlite_atomic_store_32(ptr, val)
    #define SQLITE_TINY_LOCK_LOAD(ptr)                 sqlite_atomic_load_32(ptr)
#else
    typedef char sqlite3_tiny_lock_state_t;
    #define SQLITE_TINY_LOCK_CAS_WEAK(ptr, exp, des)   sqlite_atomic_cas_weak_8(ptr, exp, des)
    #define SQLITE_TINY_LOCK_CAS_STRONG(ptr, exp, des) sqlite_atomic_cas_strong_8(ptr, exp, des)
    #define SQLITE_TINY_LOCK_STORE(ptr, val)           sqlite_atomic_store_8(ptr, val)
    #define SQLITE_TINY_LOCK_LOAD(ptr)                 sqlite_atomic_load_8(ptr)
#endif

typedef struct {
    // 0 = Unlocked, 1 = Locked.
    // 4 bytes on WASM (for memory.atomic.wait32), 1 byte on native architectures.
    sqlite3_tiny_lock_state_t state;
} sqlite3_tiny_lock;

/** 
 * @brief Initializes the lock state. Must be called exactly once before use. 
 */
static inline void sqlite3_tiny_lock_init(sqlite3_tiny_lock* lock) {
    lock->state = 0;
}

/** 
 * @brief Blocks the current thread until the lock is successfully acquired. 
 */
static inline void sqlite3_tiny_lock_lock(sqlite3_tiny_lock* lock) {
    sqlite3_tiny_lock_state_t expected = 0;
    
    // Outer loop tries to CAS (Test-And-Set)
    while (!SQLITE_TINY_LOCK_CAS_WEAK(&lock->state, &expected, 1)) {
        expected = 0;
        
#if defined(__wasm__)
        // WASM natively waits/sleeps at the engine level without needing a spin loop
        SQLITE_CPU_RELAX(&lock->state);
#else
        // TTAS (Test and Test-And-Set): Pure read inner loop to prevent cache bouncing
        while (SQLITE_TINY_LOCK_LOAD(&lock->state) == 1) {
            SQLITE_CPU_RELAX(&lock->state);
        }
#endif
    }
}

/** 
 * @brief Attempts to acquire the lock immediately without blocking. 
 * @return 1 (True) if successful, 0 (False) if already locked by another thread.
 */
static inline int sqlite3_tiny_lock_try_lock(sqlite3_tiny_lock* lock) {
    sqlite3_tiny_lock_state_t expected = 0;
    // We use STRONG CAS here because there is no retry loop.
    return SQLITE_TINY_LOCK_CAS_STRONG(&lock->state, &expected, 1);
}

/** 
 * @brief Releases the lock, allowing other threads to acquire it. 
 */
static inline void sqlite3_tiny_lock_unlock(sqlite3_tiny_lock* lock) {
    // Atomically reset the state back to 0 (Unlocked)
    SQLITE_TINY_LOCK_STORE(&lock->state, 0);

    // WEBASSEMBLY: Signal the engine to wake up any threads that went 
    // to sleep during wait32. (No-op on native CPUs).
    SQLITE_CPU_NOTIFY(&lock->state);
}

// Uniform locking adapters for generic state manager integration
#define sqlite3_tiny_lock_destroy(lock_ptr)        ((void)0)
#define sqlite3_tiny_lock_read_acquire(lock_ptr)   sqlite3_tiny_lock_lock(lock_ptr)
#define sqlite3_tiny_lock_read_release(lock_ptr)   sqlite3_tiny_lock_unlock(lock_ptr)
#define sqlite3_tiny_lock_write_acquire(lock_ptr)  sqlite3_tiny_lock_lock(lock_ptr)
#define sqlite3_tiny_lock_write_release(lock_ptr)  sqlite3_tiny_lock_unlock(lock_ptr)

#ifdef __cplusplus
}
#endif

#endif // SQLITE3_TINY_LOCK_H
