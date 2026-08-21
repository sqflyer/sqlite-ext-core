#ifndef SQLITE3_ALLOCATOR_HPP
#define SQLITE3_ALLOCATOR_HPP

#include <sqlite3.h>

/**
 * @brief Utility templates to enable zero-dependency move semantics.
 */
template<typename T> struct sqlite_remove_reference { typedef T type; };
template<typename T> struct sqlite_remove_reference<T&> { typedef T type; };
template<typename T> struct sqlite_remove_reference<T&&> { typedef T type; };

/**
 * @brief Performs a zero-dependency move cast, equivalent to `std::move`.
 * 
 * Casts an lvalue to an rvalue reference, allowing the compiler to invoke move constructors
 * and move assignment operators without linking to the standard library `<utility>`.
 * 
 * @param arg The object to be moved.
 * @return An rvalue reference to the object.
 */
template<typename T>
inline typename sqlite_remove_reference<T>::type&& sqlite_move_ptr(T&& arg) noexcept {
    return static_cast<typename sqlite_remove_reference<T>::type&&>(arg);
}

/**
 * @brief Perfect forwarding cast for lvalues, equivalent to `std::forward`.
 * 
 * Used internally by variadic templates (like `sqlite_new`) to perfectly preserve the 
 * value category (lvalue or rvalue) of the arguments when passing them to a constructor.
 * 
 * @param arg The argument to forward.
 * @return The perfectly forwarded argument.
 */
template <typename T>
inline T&& sqlite_forward(typename sqlite_remove_reference<T>::type& arg) noexcept {
    return static_cast<T&&>(arg);
}

/**
 * @brief Perfect forwarding cast for rvalues, equivalent to `std::forward`.
 */
template <typename T>
inline T&& sqlite_forward(typename sqlite_remove_reference<T>::type&& arg) noexcept {
    return static_cast<T&&>(arg);
}

// ----------------------------------------------------------------------------
// NO-STD PLACEMENT NEW
// ----------------------------------------------------------------------------

/**
 * @brief A dummy tag struct used exclusively to trigger our custom placement-new operator.
 */
struct sqlite_new_tag {};

/**
 * @brief Custom placement-new operator for zero-dependency constructor invocation.
 * 
 * By defining this globally with our proprietary tag (`sqlite_new_tag`), we 
 * instruct the C++ compiler how to evaluate `new (ptr, tag) Type()` syntax natively. 
 * This completely bypasses the standard library `<new>` header, preventing linker errors 
 * in strict `-nostdlib++` or `-fno-exceptions` environments.
 * 
 * @param p The pre-allocated raw memory address (e.g., from `sqlite3_malloc`).
 * @return The exact same pointer `p`, ready for the compiler to invoke the constructor on.
 */
inline void* operator new(size_t, void* p, sqlite_new_tag) noexcept {
    return p;
}

/**
 * @brief Constructs an object of type T in-place at the given memory address.
 * Hides the boilerplate of the custom placement-new tag. Equivalent to C++20 std::construct_at.
 */
template <typename T, typename... Args>
inline T* sqlite_construct_at(T* p, Args&&... args) {
    return new (p, sqlite_new_tag{}) T(sqlite_forward<Args>(args)...);
}

/**
 * @brief Allocates memory using sqlite3_malloc and calls the constructor via our custom placement new.
 * Mimics standard `new` but integrates natively with SQLite's memory profiler.
 */
template <typename T, typename... Args>
inline T* sqlite_new(Args&&... args) {
    void* mem = sqlite3_malloc(sizeof(T));
    if (!mem) return nullptr;
    return sqlite_construct_at(static_cast<T*>(mem), sqlite_forward<Args>(args)...);
}

/**
 * @brief Calls the pseudo-destructor and frees memory using sqlite3_free.
 * Mimics standard `delete`.
 */
template <typename T>
inline void sqlite_delete(T* ptr) {
    if (ptr) {
        ptr->~T();
        sqlite3_free(ptr);
    }
}

/**
 * @brief Invokes the destructor of an object in-place without freeing its memory.
 * Equivalent to C++20 std::destroy_at.
 */
template <typename T>
inline void sqlite_destroy_at(T* p) noexcept {
    if (p) {
        p->~T();
    }
}

/**
 * @brief Invokes destructors for N elements in a contiguous buffer.
 * Equivalent to C++20 std::destroy_n.
 */
template <typename T>
inline void sqlite_destroy_n(T* first, size_t count) noexcept {
    if (!first) return;
    // Standard C++ guarantees arrays are destructed in reverse order
    for (size_t i = count; i > 0; --i) {
        sqlite_destroy_at(&first[i - 1]);
    }
}

/**
 * @brief Allocates raw memory for an array of objects.
 * Note: Does NOT call C++ constructors. The memory is uninitialized.
 * 
 * @param count The number of elements to allocate.
 * @return A pointer to the first element of the raw array.
 */
template <typename T>
inline T* sqlite_new_array(size_t count) {
    if (count == 0) return nullptr;
    // Check for size_t multiplication overflow:
    if (count > static_cast<size_t>(-1) / sizeof(T)) {
        return nullptr; // Out of memory / overflow prevented
    }
    return static_cast<T*>(sqlite3_malloc(sizeof(T) * count));
}

/**
 * @brief Frees the memory of an array.
 * Note: Does NOT call C++ destructors.
 * 
 * @param arr The array pointer.
 */
template <typename T>
inline void sqlite_delete_array(T* arr) {
    if (arr) {
        sqlite3_free(arr);
    }
}

#endif // SQLITE3_ALLOCATOR_HPP
