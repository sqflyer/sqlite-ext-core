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
    static inline int __sqlite_atomic_cas_8(volatile char* ptr, char* expected, char desired) {
        char prev = _InterlockedCompareExchange8(ptr, desired, *expected);
        if (prev == *expected) return 1;
        *expected = prev;
        return 0;
    }

    static inline int __sqlite_atomic_cas_16(volatile short* ptr, short* expected, short desired) {
        short prev = _InterlockedCompareExchange16(ptr, desired, *expected);
        if (prev == *expected) return 1;
        *expected = prev;
        return 0;
    }

    static inline int __sqlite_atomic_cas_32(volatile long* ptr, int* expected, long desired) {
        long prev = _InterlockedCompareExchange(ptr, desired, (long)*expected);
        if (prev == *expected) return 1;
        *expected = (int)prev;
        return 0;
    }

    static inline int __sqlite_atomic_cas_64(volatile __int64* ptr, __int64* expected, __int64 desired) {
        __int64 prev = _InterlockedCompareExchange64(ptr, desired, *expected);
        if (prev == *expected) return 1;
        *expected = prev;
        return 0;
    }

    static inline int __sqlite_atomic_cas_ptr(void* volatile* ptr, void** expected, void* desired) {
        void* prev = _InterlockedCompareExchangePointer(ptr, desired, *expected);
        if (prev == *expected) return 1;
        *expected = prev;
        return 0;
    }

    #define sqlite_atomic_cas_weak_8(ptr, expected, desired)   __sqlite_atomic_cas_8((volatile char*)(ptr), (char*)(expected), (char)(desired))
    #define sqlite_atomic_cas_strong_8(ptr, expected, desired) sqlite_atomic_cas_weak_8(ptr, expected, desired)
    
    #define sqlite_atomic_cas_weak_16(ptr, expected, desired)  __sqlite_atomic_cas_16((volatile short*)(ptr), (short*)(expected), (short)(desired))
    #define sqlite_atomic_cas_strong_16(ptr, expected, desired) sqlite_atomic_cas_weak_16(ptr, expected, desired)
    
    #define sqlite_atomic_cas_weak_32(ptr, expected, desired)  __sqlite_atomic_cas_32((volatile long*)(ptr), (int*)(expected), (long)(desired))
    #define sqlite_atomic_cas_strong_32(ptr, expected, desired) sqlite_atomic_cas_weak_32(ptr, expected, desired)
    
    #define sqlite_atomic_cas_weak_64(ptr, expected, desired)  __sqlite_atomic_cas_64((volatile __int64*)(ptr), (__int64*)(expected), (__int64)(desired))
    #define sqlite_atomic_cas_strong_64(ptr, expected, desired) sqlite_atomic_cas_weak_64(ptr, expected, desired)

    #define sqlite_atomic_cas_weak_ptr(ptr, expected, desired) __sqlite_atomic_cas_ptr((void* volatile*)(ptr), (void**)(expected), (void*)(desired))
    #define sqlite_atomic_cas_strong_ptr(ptr, expected, desired) sqlite_atomic_cas_weak_ptr(ptr, expected, desired)

    // STORES
    #define sqlite_atomic_store_8(ptr, val)   _InterlockedExchange8((volatile char*)(ptr), (val))
    #define sqlite_atomic_store_16(ptr, val)  _InterlockedExchange16((volatile short*)(ptr), (val))
    #define sqlite_atomic_store_32(ptr, val)  _InterlockedExchange((volatile long*)(ptr), (val))
    #define sqlite_atomic_store_64(ptr, val)  _InterlockedExchange64((volatile __int64*)(ptr), (val))
    #define sqlite_atomic_store_ptr(ptr, val) _InterlockedExchangePointer((void* volatile*)(ptr), (void*)(val))

    // LOADS (Returns current value with memory barriers)
    #define sqlite_atomic_load_8(ptr)   _InterlockedOr8((volatile char*)(ptr), 0)
    #define sqlite_atomic_load_16(ptr)  _InterlockedOr16((volatile short*)(ptr), 0)
    #define sqlite_atomic_load_32(ptr)  _InterlockedOr((volatile long*)(ptr), 0)
    #define sqlite_atomic_load_64(ptr)  _InterlockedOr64((volatile __int64*)(ptr), 0)
    static inline void* __sqlite_atomic_load_ptr(void* volatile* ptr) {
        return _InterlockedCompareExchangePointer(ptr, (void*)0, (void*)0);
    }
    #define sqlite_atomic_load_ptr(ptr) __sqlite_atomic_load_ptr((void* volatile*)(ptr))

    // EXCHANGES (Unconditionally stores new value and returns old value)
    #define sqlite_atomic_exchange_8(ptr, val)   _InterlockedExchange8((volatile char*)(ptr), (val))
    #define sqlite_atomic_exchange_16(ptr, val)  _InterlockedExchange16((volatile short*)(ptr), (val))
    #define sqlite_atomic_exchange_32(ptr, val)  _InterlockedExchange((volatile long*)(ptr), (val))
    #define sqlite_atomic_exchange_64(ptr, val)  _InterlockedExchange64((volatile __int64*)(ptr), (val))
    #define sqlite_atomic_exchange_ptr(ptr, val) _InterlockedExchangePointer((void* volatile*)(ptr), (void*)(val))

    // FETCH ADD / SUB (Returns old value)
    #define sqlite_atomic_fetch_add_8(ptr, val)  _InterlockedExchangeAdd8((volatile char*)(ptr), (val))
    #define sqlite_atomic_fetch_sub_8(ptr, val)  _InterlockedExchangeAdd8((volatile char*)(ptr), -(val))
    #define sqlite_atomic_fetch_add_16(ptr, val) _InterlockedExchangeAdd16((volatile short*)(ptr), (val))
    #define sqlite_atomic_fetch_sub_16(ptr, val) _InterlockedExchangeAdd16((volatile short*)(ptr), -(val))
    #define sqlite_atomic_fetch_add_32(ptr, val) _InterlockedExchangeAdd((volatile long*)(ptr), (val))
    #define sqlite_atomic_fetch_sub_32(ptr, val) _InterlockedExchangeAdd((volatile long*)(ptr), -(val))
    #define sqlite_atomic_fetch_add_64(ptr, val) _InterlockedExchangeAdd64((volatile __int64*)(ptr), (val))
    #define sqlite_atomic_fetch_sub_64(ptr, val) _InterlockedExchangeAdd64((volatile __int64*)(ptr), -(val))

    // INCREMENT / DECREMENT (Returns new value)
    static inline char __sqlite_atomic_inc_8(volatile char* ptr) {
        // Adds 1, returns the OLD value. We add 1 to return the NEW value.
        return _InterlockedExchangeAdd8(ptr, 1) + 1;
    }
    static inline char __sqlite_atomic_dec_8(volatile char* ptr) {
        // Subtracts 1, returns the OLD value. We subtract 1 to return the NEW value.
        return _InterlockedExchangeAdd8(ptr, -1) - 1;
    }
    #define sqlite_atomic_increment_8(ptr)  __sqlite_atomic_inc_8((volatile char*)(ptr))
    #define sqlite_atomic_decrement_8(ptr)  __sqlite_atomic_dec_8((volatile char*)(ptr))
    
    #define sqlite_atomic_increment_16(ptr) _InterlockedIncrement16((volatile short*)(ptr))
    #define sqlite_atomic_decrement_16(ptr) _InterlockedDecrement16((volatile short*)(ptr))
    #define sqlite_atomic_increment_32(ptr) _InterlockedIncrement((volatile long*)(ptr))
    #define sqlite_atomic_decrement_32(ptr) _InterlockedDecrement((volatile long*)(ptr))
    #define sqlite_atomic_increment_64(ptr) _InterlockedIncrement64((volatile __int64*)(ptr))
    #define sqlite_atomic_decrement_64(ptr) _InterlockedDecrement64((volatile __int64*)(ptr))

    // BITWISE OPERATIONS (Returns old value)
    #define sqlite_atomic_fetch_and_8(ptr, val)  _InterlockedAnd8((volatile char*)(ptr), (val))
    #define sqlite_atomic_fetch_or_8(ptr, val)   _InterlockedOr8((volatile char*)(ptr), (val))
    #define sqlite_atomic_fetch_xor_8(ptr, val)  _InterlockedXor8((volatile char*)(ptr), (val))

    #define sqlite_atomic_fetch_and_16(ptr, val) _InterlockedAnd16((volatile short*)(ptr), (val))
    #define sqlite_atomic_fetch_or_16(ptr, val)  _InterlockedOr16((volatile short*)(ptr), (val))
    #define sqlite_atomic_fetch_xor_16(ptr, val) _InterlockedXor16((volatile short*)(ptr), (val))

    #define sqlite_atomic_fetch_and_32(ptr, val) _InterlockedAnd((volatile long*)(ptr), (val))
    #define sqlite_atomic_fetch_or_32(ptr, val)  _InterlockedOr((volatile long*)(ptr), (val))
    #define sqlite_atomic_fetch_xor_32(ptr, val) _InterlockedXor((volatile long*)(ptr), (val))

    #define sqlite_atomic_fetch_and_64(ptr, val) _InterlockedAnd64((volatile __int64*)(ptr), (val))
    #define sqlite_atomic_fetch_or_64(ptr, val)  _InterlockedOr64((volatile __int64*)(ptr), (val))
    #define sqlite_atomic_fetch_xor_64(ptr, val) _InterlockedXor64((volatile __int64*)(ptr), (val))

#else
    // --- GCC / CLANG COMPILERS ---
    
    // GCC/Clang built-ins are polymorphic and automatically size themselves based on the pointer type!
    // However, to keep the API consistent with MSVC, we define explicitly sized macros.
    
    // Weak CAS: Faster in loops. Can fail spuriously on some architectures.
    #define sqlite_atomic_cas_weak_8(ptr, expected, desired)   __atomic_compare_exchange_n((ptr), (expected), (desired), 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
    #define sqlite_atomic_cas_weak_16(ptr, expected, desired)  __atomic_compare_exchange_n((ptr), (expected), (desired), 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
    #define sqlite_atomic_cas_weak_32(ptr, expected, desired)  __atomic_compare_exchange_n((ptr), (expected), (desired), 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
    #define sqlite_atomic_cas_weak_64(ptr, expected, desired)  __atomic_compare_exchange_n((ptr), (expected), (desired), 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
    #define sqlite_atomic_cas_weak_ptr(ptr, expected, desired) __atomic_compare_exchange_n((ptr), (expected), (desired), 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)

    // Strong CAS: Slower, but guaranteed not to fail spuriously.
    #define sqlite_atomic_cas_strong_8(ptr, expected, desired)   __atomic_compare_exchange_n((ptr), (expected), (desired), 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
    #define sqlite_atomic_cas_strong_16(ptr, expected, desired)  __atomic_compare_exchange_n((ptr), (expected), (desired), 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
    #define sqlite_atomic_cas_strong_32(ptr, expected, desired)  __atomic_compare_exchange_n((ptr), (expected), (desired), 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
    #define sqlite_atomic_cas_strong_64(ptr, expected, desired)  __atomic_compare_exchange_n((ptr), (expected), (desired), 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
    #define sqlite_atomic_cas_strong_ptr(ptr, expected, desired) __atomic_compare_exchange_n((ptr), (expected), (desired), 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)

    // Stores
    #define sqlite_atomic_store_8(ptr, val)   __atomic_store_n((ptr), (val), __ATOMIC_RELEASE)
    #define sqlite_atomic_store_16(ptr, val)  __atomic_store_n((ptr), (val), __ATOMIC_RELEASE)
    #define sqlite_atomic_store_32(ptr, val)  __atomic_store_n((ptr), (val), __ATOMIC_RELEASE)
    #define sqlite_atomic_store_64(ptr, val)  __atomic_store_n((ptr), (val), __ATOMIC_RELEASE)
    #define sqlite_atomic_store_ptr(ptr, val) __atomic_store_n((ptr), (val), __ATOMIC_RELEASE)

    // LOADS (Returns current value with memory barriers)
    #define sqlite_atomic_load_8(ptr)   __atomic_load_n((ptr), __ATOMIC_ACQUIRE)
    #define sqlite_atomic_load_16(ptr)  __atomic_load_n((ptr), __ATOMIC_ACQUIRE)
    #define sqlite_atomic_load_32(ptr)  __atomic_load_n((ptr), __ATOMIC_ACQUIRE)
    #define sqlite_atomic_load_64(ptr)  __atomic_load_n((ptr), __ATOMIC_ACQUIRE)
    #define sqlite_atomic_load_ptr(ptr) __atomic_load_n((ptr), __ATOMIC_ACQUIRE)

    // EXCHANGES (Unconditionally stores new value and returns old value)
    #define sqlite_atomic_exchange_8(ptr, val)   __atomic_exchange_n((ptr), (val), __ATOMIC_ACQ_REL)
    #define sqlite_atomic_exchange_16(ptr, val)  __atomic_exchange_n((ptr), (val), __ATOMIC_ACQ_REL)
    #define sqlite_atomic_exchange_32(ptr, val)  __atomic_exchange_n((ptr), (val), __ATOMIC_ACQ_REL)
    #define sqlite_atomic_exchange_64(ptr, val)  __atomic_exchange_n((ptr), (val), __ATOMIC_ACQ_REL)
    #define sqlite_atomic_exchange_ptr(ptr, val) __atomic_exchange_n((ptr), (val), __ATOMIC_ACQ_REL)

    // FETCH ADD / SUB (Returns old value)
    #define sqlite_atomic_fetch_add_8(ptr, val)  __atomic_fetch_add((ptr), (val), __ATOMIC_SEQ_CST)
    #define sqlite_atomic_fetch_sub_8(ptr, val)  __atomic_fetch_sub((ptr), (val), __ATOMIC_SEQ_CST)
    #define sqlite_atomic_fetch_add_16(ptr, val) __atomic_fetch_add((ptr), (val), __ATOMIC_SEQ_CST)
    #define sqlite_atomic_fetch_sub_16(ptr, val) __atomic_fetch_sub((ptr), (val), __ATOMIC_SEQ_CST)
    #define sqlite_atomic_fetch_add_32(ptr, val) __atomic_fetch_add((ptr), (val), __ATOMIC_SEQ_CST)
    #define sqlite_atomic_fetch_sub_32(ptr, val) __atomic_fetch_sub((ptr), (val), __ATOMIC_SEQ_CST)
    #define sqlite_atomic_fetch_add_64(ptr, val) __atomic_fetch_add((ptr), (val), __ATOMIC_SEQ_CST)
    #define sqlite_atomic_fetch_sub_64(ptr, val) __atomic_fetch_sub((ptr), (val), __ATOMIC_SEQ_CST)

    // INCREMENT / DECREMENT (Returns new value)
    #define sqlite_atomic_increment_8(ptr)  __atomic_add_fetch((ptr), 1, __ATOMIC_SEQ_CST)
    #define sqlite_atomic_decrement_8(ptr)  __atomic_sub_fetch((ptr), 1, __ATOMIC_SEQ_CST)
    #define sqlite_atomic_increment_16(ptr) __atomic_add_fetch((ptr), 1, __ATOMIC_SEQ_CST)
    #define sqlite_atomic_decrement_16(ptr) __atomic_sub_fetch((ptr), 1, __ATOMIC_SEQ_CST)
    #define sqlite_atomic_increment_32(ptr) __atomic_add_fetch((ptr), 1, __ATOMIC_SEQ_CST)
    #define sqlite_atomic_decrement_32(ptr) __atomic_sub_fetch((ptr), 1, __ATOMIC_SEQ_CST)
    #define sqlite_atomic_increment_64(ptr) __atomic_add_fetch((ptr), 1, __ATOMIC_SEQ_CST)
    #define sqlite_atomic_decrement_64(ptr) __atomic_sub_fetch((ptr), 1, __ATOMIC_SEQ_CST)

    // BITWISE OPERATIONS (Returns old value)
    #define sqlite_atomic_fetch_and_8(ptr, val)  __atomic_fetch_and((ptr), (val), __ATOMIC_SEQ_CST)
    #define sqlite_atomic_fetch_or_8(ptr, val)   __atomic_fetch_or((ptr), (val), __ATOMIC_SEQ_CST)
    #define sqlite_atomic_fetch_xor_8(ptr, val)  __atomic_fetch_xor((ptr), (val), __ATOMIC_SEQ_CST)

    #define sqlite_atomic_fetch_and_16(ptr, val) __atomic_fetch_and((ptr), (val), __ATOMIC_SEQ_CST)
    #define sqlite_atomic_fetch_or_16(ptr, val)  __atomic_fetch_or((ptr), (val), __ATOMIC_SEQ_CST)
    #define sqlite_atomic_fetch_xor_16(ptr, val) __atomic_fetch_xor((ptr), (val), __ATOMIC_SEQ_CST)

    #define sqlite_atomic_fetch_and_32(ptr, val) __atomic_fetch_and((ptr), (val), __ATOMIC_SEQ_CST)
    #define sqlite_atomic_fetch_or_32(ptr, val)  __atomic_fetch_or((ptr), (val), __ATOMIC_SEQ_CST)
    #define sqlite_atomic_fetch_xor_32(ptr, val) __atomic_fetch_xor((ptr), (val), __ATOMIC_SEQ_CST)

    #define sqlite_atomic_fetch_and_64(ptr, val) __atomic_fetch_and((ptr), (val), __ATOMIC_SEQ_CST)
    #define sqlite_atomic_fetch_or_64(ptr, val)  __atomic_fetch_or((ptr), (val), __ATOMIC_SEQ_CST)
    #define sqlite_atomic_fetch_xor_64(ptr, val) __atomic_fetch_xor((ptr), (val), __ATOMIC_SEQ_CST)

#endif

#ifdef __cplusplus
}
#endif

#endif // SQLITE3_ATOMIC_H
