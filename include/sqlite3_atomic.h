#ifndef SQLITE3_ATOMIC_H
#define SQLITE3_ATOMIC_H

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// ATOMIC COMPILER INTRINSICS
// ============================================================================
// Zero-dependency wrappers around compiler-specific atomic instructions.

#if defined(_MSC_VER)
    // --- MICROSOFT VISUAL STUDIO COMPILER ---
    #include <intrin.h>
    
    // MSVC Interlocked functions do not differentiate between weak and strong CAS. 
    // They are always strong. 1 = success, 0 = failure.
    
    /**
     * @brief MSVC Compare-And-Swap (CAS) Implementation
     * 
     * We use static inline functions instead of pure macros to prevent a 
     * critical Time-Of-Check to Time-Of-Use (TOCTOU) race condition.
     * 
     * If a CAS fails, standard C++ requires that the `expected` variable is 
     * updated with the actual value currently in memory. If we implemented this 
     * as a macro doing a secondary read (e.g. `*expected = *ptr`), another thread 
     * could change `*ptr` in between the failed CAS and the read, loading the 
     * wrong state into `expected` and breaking spinloop logic.
     * 
     * These functions safely capture the exact `prev` value returned by 
     * `_InterlockedCompareExchange` and write it to `expected` atomically.
     */
    static inline int sqlite_atomic_cas_8(volatile char* ptr, char* expected, char desired) {
        char prev = _InterlockedCompareExchange8(ptr, desired, *expected);
        if (prev == *expected) return 1;
        *expected = prev;
        return 0;
    }

    static inline int sqlite_atomic_cas_16(volatile short* ptr, short* expected, short desired) {
        short prev = _InterlockedCompareExchange16(ptr, desired, *expected);
        if (prev == *expected) return 1;
        *expected = prev;
        return 0;
    }

    static inline int sqlite_atomic_cas_32(volatile long* ptr, int* expected, long desired) {
        long prev = _InterlockedCompareExchange(ptr, desired, (long)*expected);
        if (prev == *expected) return 1;
        *expected = (int)prev;
        return 0;
    }

    static inline int sqlite_atomic_cas_64(volatile __int64* ptr, __int64* expected, __int64 desired) {
        __int64 prev = _InterlockedCompareExchange64(ptr, desired, *expected);
        if (prev == *expected) return 1;
        *expected = prev;
        return 0;
    }

    static inline int sqlite_atomic_cas_ptr(void* volatile* ptr, void** expected, void* desired) {
        void* prev = _InterlockedCompareExchangePointer(ptr, desired, *expected);
        if (prev == *expected) return 1;
        *expected = prev;
        return 0;
    }

    #define SQLITE_ATOMIC_CAS_WEAK_8(ptr, expected, desired)   sqlite_atomic_cas_8((volatile char*)(ptr), (char*)(expected), (char)(desired))
    #define SQLITE_ATOMIC_CAS_STRONG_8(ptr, expected, desired) SQLITE_ATOMIC_CAS_WEAK_8(ptr, expected, desired)
    
    #define SQLITE_ATOMIC_CAS_WEAK_16(ptr, expected, desired)  sqlite_atomic_cas_16((volatile short*)(ptr), (short*)(expected), (short)(desired))
    #define SQLITE_ATOMIC_CAS_STRONG_16(ptr, expected, desired) SQLITE_ATOMIC_CAS_WEAK_16(ptr, expected, desired)
    
    #define SQLITE_ATOMIC_CAS_WEAK_32(ptr, expected, desired)  sqlite_atomic_cas_32((volatile long*)(ptr), (int*)(expected), (long)(desired))
    #define SQLITE_ATOMIC_CAS_STRONG_32(ptr, expected, desired) SQLITE_ATOMIC_CAS_WEAK_32(ptr, expected, desired)
    
    #define SQLITE_ATOMIC_CAS_WEAK_64(ptr, expected, desired)  sqlite_atomic_cas_64((volatile __int64*)(ptr), (__int64*)(expected), (__int64)(desired))
    #define SQLITE_ATOMIC_CAS_STRONG_64(ptr, expected, desired) SQLITE_ATOMIC_CAS_WEAK_64(ptr, expected, desired)

    #define SQLITE_ATOMIC_CAS_WEAK_PTR(ptr, expected, desired) sqlite_atomic_cas_ptr((void* volatile*)(ptr), (void**)(expected), (void*)(desired))
    #define SQLITE_ATOMIC_CAS_STRONG_PTR(ptr, expected, desired) SQLITE_ATOMIC_CAS_WEAK_PTR(ptr, expected, desired)

    // STORES
    #define SQLITE_ATOMIC_STORE_8(ptr, val)   _InterlockedExchange8((volatile char*)(ptr), (val))
    #define SQLITE_ATOMIC_STORE_16(ptr, val)  _InterlockedExchange16((volatile short*)(ptr), (val))
    #define SQLITE_ATOMIC_STORE_32(ptr, val)  _InterlockedExchange((volatile long*)(ptr), (val))
    #define SQLITE_ATOMIC_STORE_64(ptr, val)  _InterlockedExchange64((volatile __int64*)(ptr), (val))
    #define SQLITE_ATOMIC_STORE_PTR(ptr, val) _InterlockedExchangePointer((void* volatile*)(ptr), (void*)(val))

#else
    // --- GCC / CLANG COMPILERS ---
    
    // GCC/Clang built-ins are polymorphic and automatically size themselves based on the pointer type!
    // However, to keep the API consistent with MSVC, we define explicitly sized macros.
    
    // Weak CAS: Faster in loops. Can fail spuriously on some architectures.
    #define SQLITE_ATOMIC_CAS_WEAK_8(ptr, expected, desired)   __atomic_compare_exchange_n((ptr), (expected), (desired), 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
    #define SQLITE_ATOMIC_CAS_WEAK_16(ptr, expected, desired)  __atomic_compare_exchange_n((ptr), (expected), (desired), 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
    #define SQLITE_ATOMIC_CAS_WEAK_32(ptr, expected, desired)  __atomic_compare_exchange_n((ptr), (expected), (desired), 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
    #define SQLITE_ATOMIC_CAS_WEAK_64(ptr, expected, desired)  __atomic_compare_exchange_n((ptr), (expected), (desired), 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
    #define SQLITE_ATOMIC_CAS_WEAK_PTR(ptr, expected, desired) __atomic_compare_exchange_n((ptr), (expected), (desired), 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)

    // Strong CAS: Slower, but guaranteed not to fail spuriously.
    #define SQLITE_ATOMIC_CAS_STRONG_8(ptr, expected, desired)   __atomic_compare_exchange_n((ptr), (expected), (desired), 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
    #define SQLITE_ATOMIC_CAS_STRONG_16(ptr, expected, desired)  __atomic_compare_exchange_n((ptr), (expected), (desired), 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
    #define SQLITE_ATOMIC_CAS_STRONG_32(ptr, expected, desired)  __atomic_compare_exchange_n((ptr), (expected), (desired), 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
    #define SQLITE_ATOMIC_CAS_STRONG_64(ptr, expected, desired)  __atomic_compare_exchange_n((ptr), (expected), (desired), 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
    #define SQLITE_ATOMIC_CAS_STRONG_PTR(ptr, expected, desired) __atomic_compare_exchange_n((ptr), (expected), (desired), 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)

    // Stores
    #define SQLITE_ATOMIC_STORE_8(ptr, val)   __atomic_store_n((ptr), (val), __ATOMIC_RELEASE)
    #define SQLITE_ATOMIC_STORE_16(ptr, val)  __atomic_store_n((ptr), (val), __ATOMIC_RELEASE)
    #define SQLITE_ATOMIC_STORE_32(ptr, val)  __atomic_store_n((ptr), (val), __ATOMIC_RELEASE)
    #define SQLITE_ATOMIC_STORE_64(ptr, val)  __atomic_store_n((ptr), (val), __ATOMIC_RELEASE)
    #define SQLITE_ATOMIC_STORE_PTR(ptr, val) __atomic_store_n((ptr), (val), __ATOMIC_RELEASE)

#endif

#ifdef __cplusplus
}
#endif

#endif // SQLITE3_ATOMIC_H
