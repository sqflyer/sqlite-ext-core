#ifndef SQLITE3_SMART_PTR_H
#define SQLITE3_SMART_PTR_H

#include "sqlite3ext.h"
#include "sqlite3_atomic.h"

/**
 * @brief Generates a zero-dependency, thread-safe, SQLite-memory-managed shared pointer for C.
 * 
 * @param Prefix The prefix for generated structs and functions (e.g., MyType).
 * @param Type The underlying data type being wrapped (e.g., struct MyStruct).
 * @param Destructor A function pointer or macro to free the data (e.g., sqlite3_free).
 */
#define SQLITE_SHARED_PTR_DEFINE(Prefix, Type, Destructor) \
    /** @brief Control block storing the pointer and atomic reference counts. */ \
    typedef struct { \
        Type* ptr; \
        int strong_count; \
        int weak_count; \
    } Prefix##_ControlBlock; \
    \
    /** @brief The shared pointer struct. */ \
    typedef struct { \
        Prefix##_ControlBlock* cb; \
    } Prefix##_SharedPtr; \
    \
    /** @brief The weak pointer struct. */ \
    typedef struct { \
        Prefix##_ControlBlock* cb; \
    } Prefix##_WeakPtr; \
    \
    /** @brief Creates a shared pointer taking ownership of the given pointer. */ \
    static inline Prefix##_SharedPtr Prefix##_make_shared(Type* ptr) { \
        Prefix##_SharedPtr sp; \
        sp.cb = 0; \
        if (!ptr) return sp; \
        Prefix##_ControlBlock* cb = (Prefix##_ControlBlock*)sqlite3_malloc(sizeof(Prefix##_ControlBlock)); \
        if (!cb) return sp; \
        cb->ptr = ptr; \
        cb->strong_count = 1; \
        cb->weak_count = 1; /* The strong references collectively hold 1 weak reference */ \
        sp.cb = cb; \
        return sp; \
    } \
    \
    /** @brief Clones the shared pointer, atomically incrementing the strong reference count. */ \
    static inline Prefix##_SharedPtr Prefix##_clone(Prefix##_SharedPtr sp) { \
        if (sp.cb) { \
            sqlite_atomic_increment_32(&sp.cb->strong_count); \
        } \
        return sp; \
    } \
    \
    /** @brief Moves the shared pointer, transferring ownership without touching the ref count. */ \
    static inline Prefix##_SharedPtr Prefix##_move(Prefix##_SharedPtr* sp) { \
        Prefix##_SharedPtr out; \
        out.cb = sp ? sp->cb : 0; \
        if (sp) sp->cb = 0; \
        return out; \
    } \
    \
    /** @brief Assigns a shared pointer from a move, safely releasing the destination first. */ \
    static inline void Prefix##_assign_move(Prefix##_SharedPtr* dst, Prefix##_SharedPtr* src) { \
        Prefix##_release(dst); \
        dst->cb = src ? src->cb : 0; \
        if (src) src->cb = 0; \
    } \
    \
    /** @brief Releases the shared pointer, decrementing the reference count and destroying the object if it reaches 0. */ \
    static inline void Prefix##_release(Prefix##_SharedPtr* sp) { \
        if (sp && sp->cb) { \
            Prefix##_ControlBlock* cb = sp->cb; \
            if (sqlite_atomic_decrement_32(&cb->strong_count) == 0) { \
                if (cb->ptr) { Destructor(cb->ptr); } \
                cb->ptr = 0; \
                Prefix##_WeakPtr wp; \
                wp.cb = cb; \
                Prefix##_weak_release(&wp); \
            } \
            sp->cb = 0; \
        } \
    } \
    \
    /** @brief Returns a pointer to the managed object. */ \
    static inline Type* Prefix##_get(Prefix##_SharedPtr sp) { \
        return sp.cb ? sp.cb->ptr : 0; \
    } \
    \
    /** @brief Resets the shared pointer, releasing its ownership. */ \
    static inline void Prefix##_reset(Prefix##_SharedPtr* sp) { \
        Prefix##_release(sp); \
    } \
    \
    /** @brief Creates a weak pointer from a shared pointer, incrementing the weak count. */ \
    static inline Prefix##_WeakPtr Prefix##_weak_create(Prefix##_SharedPtr sp) { \
        Prefix##_WeakPtr wp; \
        wp.cb = sp.cb; \
        if (wp.cb) { \
            sqlite_atomic_increment_32(&wp.cb->weak_count); \
        } \
        return wp; \
    } \
    \
    /** @brief Clones the weak pointer, incrementing the weak count. */ \
    static inline Prefix##_WeakPtr Prefix##_weak_clone(Prefix##_WeakPtr wp) { \
        if (wp.cb) { \
            sqlite_atomic_increment_32(&wp.cb->weak_count); \
        } \
        return wp; \
    } \
    \
    /** @brief Moves the weak pointer, transferring ownership without touching the weak count. */ \
    static inline Prefix##_WeakPtr Prefix##_weak_move(Prefix##_WeakPtr* wp) { \
        Prefix##_WeakPtr out; \
        out.cb = wp ? wp->cb : 0; \
        if (wp) wp->cb = 0; \
        return out; \
    } \
    \
    /** @brief Releases the weak pointer, decrementing the weak count. */ \
    static inline void Prefix##_weak_release(Prefix##_WeakPtr* wp) { \
        if (wp && wp->cb) { \
            if (sqlite_atomic_decrement_32(&wp->cb->weak_count) == 0) { \
                sqlite3_free(wp->cb); \
            } \
            wp->cb = 0; \
        } \
    } \
    \
    /** @brief Resets the weak pointer, releasing its ownership. */ \
    static inline void Prefix##_weak_reset(Prefix##_WeakPtr* wp) { \
        Prefix##_weak_release(wp); \
    } \
    \
    /** @brief Checks if the managed object has already been deleted. */ \
    static inline int Prefix##_weak_expired(Prefix##_WeakPtr wp) { \
        if (!wp.cb) return 1; \
        return wp.cb->strong_count == 0; \
    } \
    \
    /** @brief Upgrades to a strong shared pointer if the object is still alive. */ \
    static inline Prefix##_SharedPtr Prefix##_weak_lock(Prefix##_WeakPtr wp) { \
        Prefix##_SharedPtr sp; \
        sp.cb = 0; \
        if (!wp.cb) return sp; \
        int current = wp.cb->strong_count; \
        while (current > 0) { \
            if (sqlite_atomic_cas_weak_32(&wp.cb->strong_count, &current, current + 1)) { \
                sp.cb = wp.cb; \
                break; \
            } \
        } \
        return sp; \
    }

/**
 * @brief Generates a zero-overhead unique pointer wrapper for C.
 * 
 * @param Prefix The prefix for generated structs and functions (e.g., MyType).
 * @param Type The underlying data type being wrapped (e.g., struct MyStruct).
 * @param Destructor A function pointer or macro to free the data (e.g., sqlite3_free).
 */
#define SQLITE_UNIQUE_PTR_DEFINE(Prefix, Type, Destructor) \
    /** @brief The unique pointer struct. */ \
    typedef struct { \
        Type* ptr; \
    } Prefix##_UniquePtr; \
    \
    /** @brief Creates a unique pointer taking ownership of the given pointer. */ \
    static inline Prefix##_UniquePtr Prefix##_make_unique(Type* ptr) { \
        Prefix##_UniquePtr up; \
        up.ptr = ptr; \
        return up; \
    } \
    \
    /** @brief Moves the unique pointer, transferring ownership. */ \
    static inline Prefix##_UniquePtr Prefix##_move_unique(Prefix##_UniquePtr* up) { \
        Prefix##_UniquePtr out; \
        out.ptr = up ? up->ptr : 0; \
        if (up) up->ptr = 0; \
        return out; \
    } \
    \
    /** @brief Releases and destroys the unique pointer and its managed object. */ \
    static inline void Prefix##_release_unique(Prefix##_UniquePtr* up) { \
        if (up && up->ptr) { \
            Destructor(up->ptr); \
            up->ptr = 0; \
        } \
    } \
    \
    /** @brief Returns a pointer to the managed object. */ \
    static inline Type* Prefix##_get_unique(Prefix##_UniquePtr up) { \
        return up.ptr; \
    }

#endif // SQLITE3_SMART_PTR_H
