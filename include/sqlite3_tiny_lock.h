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

#if defined(_MSC_VER)
    #include <intrin.h>
    #define SQLITE_CPU_RELAX() _mm_pause()
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
    #define SQLITE_CPU_RELAX() _sqlite_gcc_cpu_relax()
#endif

// ============================================================================
// TINY LOCK INTERFACE
// ============================================================================

/**
 * @brief sqlite3_tiny_lock: A 4-byte, zero-dependency, cross-platform hybrid lock.
 * 
 * - Native Architecture: Acts as a blistering-fast, CPU-yielding Spinlock.
 * - WebAssembly: Acts as a true 0% CPU sleeping Mutex.
 * 
 * The struct is exactly 4 bytes, allowing it to be embedded by-value into 
 * other structures without triggering heap allocations.
 */
typedef struct {
    // 0 = Unlocked, 1 = Locked.
    // Note: Forced to be 32-bit (int) because WebAssembly's 
    // memory.atomic.wait32 instruction strictly requires 32-bit memory.
    int state;
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
    int expected = 0;
    
    // Attempt to swap 0 (Unlocked) with 1 (Locked)
    while (!SQLITE_ATOMIC_CAS_WEAK_32(&lock->state, &expected, 1)) {
        // If CAS fails, 'expected' is updated to the current state (1).
        // Reset it back to 0 for the next attempt.
        expected = 0;

#if defined(__wasm__)
        // WEBASSEMBLY: Put the thread completely to sleep (0% CPU) until 
        // the state changes. The loop handles spurious wakeups safely.
        __builtin_wasm_memory_atomic_wait32(&lock->state, 1, -1);
#else
        // NATIVE CPU: Throttle the hardware pipeline to save power and 
        // allow other threads on this core to execute.
        SQLITE_CPU_RELAX();
#endif
    }
}

/** 
 * @brief Attempts to acquire the lock immediately without blocking. 
 * @return 1 (True) if successful, 0 (False) if already locked by another thread.
 */
static inline int sqlite3_tiny_lock_try_lock(sqlite3_tiny_lock* lock) {
    int expected = 0;
    // We use STRONG CAS here because there is no retry loop.
    return SQLITE_ATOMIC_CAS_STRONG_32(&lock->state, &expected, 1);
}

/** 
 * @brief Releases the lock, allowing other threads to acquire it. 
 */
static inline void sqlite3_tiny_lock_unlock(sqlite3_tiny_lock* lock) {
    // Atomically reset the state back to 0 (Unlocked)
    SQLITE_ATOMIC_STORE_32(&lock->state, 0);

#if defined(__wasm__)
    // WEBASSEMBLY: Signal the engine to wake up any threads that went 
    // to sleep during `__builtin_wasm_memory_atomic_wait32`.
    __builtin_wasm_memory_atomic_notify(&lock->state, 1);
#endif
}

#ifdef __cplusplus
}
#endif

#endif // SQLITE3_TINY_LOCK_H
