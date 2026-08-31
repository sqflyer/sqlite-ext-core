#ifndef SQLITE3_ATOMIC_HPP
#define SQLITE3_ATOMIC_HPP

#include "sqlite3_atomic.h"
#include "sqlite3_allocator.hpp"
#include <stddef.h>
#include <stdint.h>

/**
 * @file sqlite3_atomic.hpp
 * @brief Zero-dependency C++ polymorphic wrappers for sqlite3_atomic.h
 * 
 * This header provides a modern C++ API that perfectly mimics the polymorphic 
 * nature of `<atomic>` or GCC's `__atomic` built-ins, but safely maps to the 
 * explicitly sized C macros underneath. 
 * 
 * It automatically supports all integer types, booleans, and pointers.
 * No stdlib is required (`-nostdlib++` safe).
 */

// ============================================================================
// COMPILER-SPECIFIC CASTING
// ============================================================================
// MSVC uses strict non-polymorphic types (long for 32-bit), whereas GCC uses 
// polymorphic built-ins that rely on exact-width types (int32_t) to map to the 
// correct instruction width. Casting incorrectly on GCC (e.g. to long on Linux) 
// would accidentally upgrade 32-bit atomics to 64-bit instructions!

#ifdef _MSC_VER
    #define SQLITE_ATOMIC_CAST_8 char
    #define SQLITE_ATOMIC_CAST_16 short
    #define SQLITE_ATOMIC_CAST_32 long
    #define SQLITE_ATOMIC_CAST_32_EXP int
    #define SQLITE_ATOMIC_CAST_64 __int64
#else
    #define SQLITE_ATOMIC_CAST_8 int8_t
    #define SQLITE_ATOMIC_CAST_16 int16_t
    #define SQLITE_ATOMIC_CAST_32 int32_t
    #define SQLITE_ATOMIC_CAST_32_EXP int32_t
    #define SQLITE_ATOMIC_CAST_64 int64_t
#endif

// ============================================================================
// ATOMIC OPS DISPATCHER
// ============================================================================

template<size_t Size> struct SqliteAtomicOps;

template<> struct SqliteAtomicOps<1> {
    typedef SQLITE_ATOMIC_CAST_8 c_t;
    template<typename T> static void store(volatile T* ptr, T val) { sqlite_atomic_store_8((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T load(volatile T* ptr) { return (T)sqlite_atomic_load_8((volatile c_t*)ptr); }
    template<typename T> static int cas_weak(volatile T* ptr, T* exp, T des) { return sqlite_atomic_cas_weak_8((volatile c_t*)ptr, (c_t*)exp, (c_t)des); }
    template<typename T> static int cas_strong(volatile T* ptr, T* exp, T des) { return sqlite_atomic_cas_strong_8((volatile c_t*)ptr, (c_t*)exp, (c_t)des); }
    template<typename T> static T exchange(volatile T* ptr, T val) { return (T)sqlite_atomic_exchange_8((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T increment(volatile T* ptr) { return (T)sqlite_atomic_increment_8((volatile c_t*)ptr); }
    template<typename T> static T decrement(volatile T* ptr) { return (T)sqlite_atomic_decrement_8((volatile c_t*)ptr); }
    template<typename T> static T fetch_add(volatile T* ptr, T val) { return (T)sqlite_atomic_fetch_add_8((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T fetch_sub(volatile T* ptr, T val) { return (T)sqlite_atomic_fetch_sub_8((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T fetch_or(volatile T* ptr, T val) { return (T)sqlite_atomic_fetch_or_8((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T fetch_and(volatile T* ptr, T val) { return (T)sqlite_atomic_fetch_and_8((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T fetch_xor(volatile T* ptr, T val) { return (T)sqlite_atomic_fetch_xor_8((volatile c_t*)ptr, (c_t)val); }
};

template<> struct SqliteAtomicOps<2> {
    typedef SQLITE_ATOMIC_CAST_16 c_t;
    template<typename T> static void store(volatile T* ptr, T val) { sqlite_atomic_store_16((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T load(volatile T* ptr) { return (T)sqlite_atomic_load_16((volatile c_t*)ptr); }
    template<typename T> static int cas_weak(volatile T* ptr, T* exp, T des) { return sqlite_atomic_cas_weak_16((volatile c_t*)ptr, (c_t*)exp, (c_t)des); }
    template<typename T> static int cas_strong(volatile T* ptr, T* exp, T des) { return sqlite_atomic_cas_strong_16((volatile c_t*)ptr, (c_t*)exp, (c_t)des); }
    template<typename T> static T exchange(volatile T* ptr, T val) { return (T)sqlite_atomic_exchange_16((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T increment(volatile T* ptr) { return (T)sqlite_atomic_increment_16((volatile c_t*)ptr); }
    template<typename T> static T decrement(volatile T* ptr) { return (T)sqlite_atomic_decrement_16((volatile c_t*)ptr); }
    template<typename T> static T fetch_add(volatile T* ptr, T val) { return (T)sqlite_atomic_fetch_add_16((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T fetch_sub(volatile T* ptr, T val) { return (T)sqlite_atomic_fetch_sub_16((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T fetch_or(volatile T* ptr, T val) { return (T)sqlite_atomic_fetch_or_16((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T fetch_and(volatile T* ptr, T val) { return (T)sqlite_atomic_fetch_and_16((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T fetch_xor(volatile T* ptr, T val) { return (T)sqlite_atomic_fetch_xor_16((volatile c_t*)ptr, (c_t)val); }
};

template<> struct SqliteAtomicOps<4> {
    typedef SQLITE_ATOMIC_CAST_32 c_t;
    typedef SQLITE_ATOMIC_CAST_32_EXP c_exp_t;
    template<typename T> static void store(volatile T* ptr, T val) { sqlite_atomic_store_32((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T load(volatile T* ptr) { return (T)sqlite_atomic_load_32((volatile c_t*)ptr); }
    template<typename T> static int cas_weak(volatile T* ptr, T* exp, T des) { return sqlite_atomic_cas_weak_32((volatile c_t*)ptr, (c_exp_t*)exp, (c_t)des); }
    template<typename T> static int cas_strong(volatile T* ptr, T* exp, T des) { return sqlite_atomic_cas_strong_32((volatile c_t*)ptr, (c_exp_t*)exp, (c_t)des); }
    template<typename T> static T exchange(volatile T* ptr, T val) { return (T)sqlite_atomic_exchange_32((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T increment(volatile T* ptr) { return (T)sqlite_atomic_increment_32((volatile c_t*)ptr); }
    template<typename T> static T decrement(volatile T* ptr) { return (T)sqlite_atomic_decrement_32((volatile c_t*)ptr); }
    template<typename T> static T fetch_add(volatile T* ptr, T val) { return (T)sqlite_atomic_fetch_add_32((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T fetch_sub(volatile T* ptr, T val) { return (T)sqlite_atomic_fetch_sub_32((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T fetch_or(volatile T* ptr, T val) { return (T)sqlite_atomic_fetch_or_32((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T fetch_and(volatile T* ptr, T val) { return (T)sqlite_atomic_fetch_and_32((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T fetch_xor(volatile T* ptr, T val) { return (T)sqlite_atomic_fetch_xor_32((volatile c_t*)ptr, (c_t)val); }
};

template<> struct SqliteAtomicOps<8> {
    typedef SQLITE_ATOMIC_CAST_64 c_t;
    template<typename T> static void store(volatile T* ptr, T val) { sqlite_atomic_store_64((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T load(volatile T* ptr) { return (T)sqlite_atomic_load_64((volatile c_t*)ptr); }
    template<typename T> static int cas_weak(volatile T* ptr, T* exp, T des) { return sqlite_atomic_cas_weak_64((volatile c_t*)ptr, (c_t*)exp, (c_t)des); }
    template<typename T> static int cas_strong(volatile T* ptr, T* exp, T des) { return sqlite_atomic_cas_strong_64((volatile c_t*)ptr, (c_t*)exp, (c_t)des); }
    template<typename T> static T exchange(volatile T* ptr, T val) { return (T)sqlite_atomic_exchange_64((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T increment(volatile T* ptr) { return (T)sqlite_atomic_increment_64((volatile c_t*)ptr); }
    template<typename T> static T decrement(volatile T* ptr) { return (T)sqlite_atomic_decrement_64((volatile c_t*)ptr); }
    template<typename T> static T fetch_add(volatile T* ptr, T val) { return (T)sqlite_atomic_fetch_add_64((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T fetch_sub(volatile T* ptr, T val) { return (T)sqlite_atomic_fetch_sub_64((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T fetch_or(volatile T* ptr, T val) { return (T)sqlite_atomic_fetch_or_64((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T fetch_and(volatile T* ptr, T val) { return (T)sqlite_atomic_fetch_and_64((volatile c_t*)ptr, (c_t)val); }
    template<typename T> static T fetch_xor(volatile T* ptr, T val) { return (T)sqlite_atomic_fetch_xor_64((volatile c_t*)ptr, (c_t)val); }
};

// ============================================================================
// PUBLIC POLYMORPHIC API
// ============================================================================

// STORE
template<typename T>
inline typename sqlite_enable_if<!sqlite_is_pointer<T>::value>::type sqlite_atomic_store(volatile T* ptr, T val) {
    SqliteAtomicOps<sizeof(T)>::store(ptr, val);
}
template<typename T>
inline typename sqlite_enable_if<sqlite_is_pointer<T>::value>::type sqlite_atomic_store(volatile T* ptr, T val) {
    sqlite_atomic_store_ptr((void* volatile*)ptr, (void*)val);
}

// LOAD
template<typename T>
inline typename sqlite_enable_if<!sqlite_is_pointer<T>::value, T>::type sqlite_atomic_load(volatile T* ptr) {
    return SqliteAtomicOps<sizeof(T)>::load(ptr);
}
template<typename T>
inline typename sqlite_enable_if<sqlite_is_pointer<T>::value, T>::type sqlite_atomic_load(volatile T* ptr) {
    return (T)sqlite_atomic_load_ptr((void* volatile*)ptr);
}

// CAS WEAK
template<typename T>
inline typename sqlite_enable_if<!sqlite_is_pointer<T>::value, int>::type sqlite_atomic_cas_weak(volatile T* ptr, T* exp, T des) {
    return SqliteAtomicOps<sizeof(T)>::cas_weak(ptr, exp, des);
}
template<typename T>
inline typename sqlite_enable_if<sqlite_is_pointer<T>::value, int>::type sqlite_atomic_cas_weak(volatile T* ptr, T* exp, T des) {
    return sqlite_atomic_cas_weak_ptr((void* volatile*)ptr, (void**)exp, (void*)des);
}

// CAS STRONG
template<typename T>
inline typename sqlite_enable_if<!sqlite_is_pointer<T>::value, int>::type sqlite_atomic_cas_strong(volatile T* ptr, T* exp, T des) {
    return SqliteAtomicOps<sizeof(T)>::cas_strong(ptr, exp, des);
}
template<typename T>
inline typename sqlite_enable_if<sqlite_is_pointer<T>::value, int>::type sqlite_atomic_cas_strong(volatile T* ptr, T* exp, T des) {
    return sqlite_atomic_cas_strong_ptr((void* volatile*)ptr, (void**)exp, (void*)des);
}

// EXCHANGE
template<typename T>
inline typename sqlite_enable_if<!sqlite_is_pointer<T>::value, T>::type sqlite_atomic_exchange(volatile T* ptr, T val) {
    return SqliteAtomicOps<sizeof(T)>::exchange(ptr, val);
}
template<typename T>
inline typename sqlite_enable_if<sqlite_is_pointer<T>::value, T>::type sqlite_atomic_exchange(volatile T* ptr, T val) {
    return (T)sqlite_atomic_exchange_ptr((void* volatile*)ptr, (void*)val);
}

// INCREMENT
template<typename T>
inline typename sqlite_enable_if<!sqlite_is_pointer<T>::value, T>::type sqlite_atomic_increment(volatile T* ptr) {
    return SqliteAtomicOps<sizeof(T)>::increment(ptr);
}

// DECREMENT
template<typename T>
inline typename sqlite_enable_if<!sqlite_is_pointer<T>::value, T>::type sqlite_atomic_decrement(volatile T* ptr) {
    return SqliteAtomicOps<sizeof(T)>::decrement(ptr);
}

// FETCH ADD
template<typename T>
inline typename sqlite_enable_if<!sqlite_is_pointer<T>::value, T>::type sqlite_atomic_fetch_add(volatile T* ptr, T val) {
    return SqliteAtomicOps<sizeof(T)>::fetch_add(ptr, val);
}

// FETCH SUB
template<typename T>
inline typename sqlite_enable_if<!sqlite_is_pointer<T>::value, T>::type sqlite_atomic_fetch_sub(volatile T* ptr, T val) {
    return SqliteAtomicOps<sizeof(T)>::fetch_sub(ptr, val);
}

// FETCH OR
template<typename T>
inline typename sqlite_enable_if<!sqlite_is_pointer<T>::value, T>::type sqlite_atomic_fetch_or(volatile T* ptr, T val) {
    return SqliteAtomicOps<sizeof(T)>::fetch_or(ptr, val);
}

// FETCH AND
template<typename T>
inline typename sqlite_enable_if<!sqlite_is_pointer<T>::value, T>::type sqlite_atomic_fetch_and(volatile T* ptr, T val) {
    return SqliteAtomicOps<sizeof(T)>::fetch_and(ptr, val);
}

// FETCH XOR
template<typename T>
inline typename sqlite_enable_if<!sqlite_is_pointer<T>::value, T>::type sqlite_atomic_fetch_xor(volatile T* ptr, T val) {
    return SqliteAtomicOps<sizeof(T)>::fetch_xor(ptr, val);
}

// ============================================================================
// C++11 RAII ATOMIC OBJECT WRAPPER (Mimicking std::atomic<T>)
// ============================================================================

/**
 * @class SqliteAtomic
 * @brief Zero-dependency C++11 atomic object wrapper mimicking `std::atomic<T>`.
 *
 * Fully compliant with `-nostdlib++` and `-fno-exceptions`. Supports arithmetic,
 * bitwise, exchange, and CAS operations with inline memory barriers.
 *
 * @tparam T Trivially copyable value or pointer type.
 */
template <typename T>
class SqliteAtomic {
private:
    volatile T m_val;

public:
    /** @brief Default constructor (uninitialized or zero-initialized). */
    inline SqliteAtomic() noexcept : m_val(T()) {}

    /** @brief Value constructor. */
    inline SqliteAtomic(T val) noexcept : m_val(val) {}

    // Non-copyable
    SqliteAtomic(const SqliteAtomic&) = delete;
    SqliteAtomic& operator=(const SqliteAtomic&) = delete;

    /** @brief Atomically loads the current value. */
    inline T load() const noexcept {
        return sqlite_atomic_load(&m_val);
    }

    /** @brief Implicit conversion operator for atomic load. */
    inline operator T() const noexcept {
        return load();
    }

    /** @brief Atomically stores a new value. */
    inline void store(T val) noexcept {
        sqlite_atomic_store(&m_val, val);
    }

    /** @brief Assignment operator for atomic store. */
    inline T operator=(T val) noexcept {
        store(val);
        return val;
    }

    /** @brief Atomically exchanges the value and returns the old value. */
    inline T exchange(T val) noexcept {
        return sqlite_atomic_exchange(&m_val, val);
    }

    /** @brief Atomically performs a strong Compare-And-Swap. */
    inline bool compare_exchange_strong(T& expected, T desired) noexcept {
        return sqlite_atomic_cas_strong(&m_val, &expected, desired) != 0;
    }

    /** @brief Atomically performs a weak Compare-And-Swap. */
    inline bool compare_exchange_weak(T& expected, T desired) noexcept {
        return sqlite_atomic_cas_weak(&m_val, &expected, desired) != 0;
    }

    /** @brief Atomically adds `val` and returns the old value. */
    inline T fetch_add(T val) noexcept {
        return sqlite_atomic_fetch_add(&m_val, val);
    }

    /** @brief Atomically subtracts `val` and returns the old value. */
    inline T fetch_sub(T val) noexcept {
        return sqlite_atomic_fetch_sub(&m_val, val);
    }

    /** @brief Pre-increment (++obj). */
    inline T operator++() noexcept {
        return sqlite_atomic_increment(&m_val);
    }

    /** @brief Post-increment (obj++). */
    inline T operator++(int) noexcept {
        return fetch_add(1);
    }

    /** @brief Pre-decrement (--obj). */
    inline T operator--() noexcept {
        return sqlite_atomic_decrement(&m_val);
    }

    /** @brief Post-decrement (obj--). */
    inline T operator--(int) noexcept {
        return fetch_sub(1);
    }

    /** @brief Compound addition. */
    inline T operator+=(T val) noexcept {
        return fetch_add(val) + val;
    }

    /** @brief Compound subtraction. */
    inline T operator-=(T val) noexcept {
        return fetch_sub(val) - val;
    }
};

/** @brief Type aliases for standard atomic types. */
typedef SqliteAtomic<int>      SqliteAtomicInt;
typedef SqliteAtomic<int64_t>  SqliteAtomicInt64;
typedef SqliteAtomic<bool>     SqliteAtomicBool;
typedef SqliteAtomic<size_t>   SqliteAtomicSize;

#endif // SQLITE3_ATOMIC_HPP
