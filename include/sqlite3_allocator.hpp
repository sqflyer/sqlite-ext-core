#ifndef SQLITE3_ALLOCATOR_HPP
#define SQLITE3_ALLOCATOR_HPP

/**
 * @file sqlite3_allocator.hpp
 * @brief Zero-dependency, freestanding memory management & type traits for SQLite extensions.
 *
 * Provides a complete replacement for standard C++ memory allocation (`<new>`, `std::allocator`),
 * move semantics (`<utility>`, `std::move`, `std::forward`), and fundamental type traits
 * (`<type_traits>`). All dynamic memory operations route strictly through SQLite's internal
 * memory profiler (`sqlite3_malloc`, `sqlite3_malloc64`, `sqlite3_free`), ensuring full
 * visibility in `sqlite3_memory_used()` and total independence from the standard C++ runtime
 * (`-nostdlib++` / `/NODEFAULTLIB`).
 */

#include <sqlite3.h>
#include <stddef.h>

// ============================================================================
// FAST MEMORY COPY MACRO
// ============================================================================

/**
 * @def SQLITE_FAST_MEMCPY
 * @brief Cross-platform, compiler-optimized memory copy intrinsic.
 *
 * Expands to `__builtin_memcpy` on GCC/Clang and `__movsb` on MSVC to guarantee
 * inline assembly code generation without linking against standard C library `memcpy`.
 */
#if defined(__GNUC__) || defined(__clang__)
    #define SQLITE_FAST_MEMCPY(dst, src, size) __builtin_memcpy((dst), (src), (size))
#elif defined(_MSC_VER)
    #include <intrin.h>
    #define SQLITE_FAST_MEMCPY(dst, src, size) __movsb(static_cast<unsigned char*>(dst), static_cast<const unsigned char*>(src), (size))
#else
    #define SQLITE_FAST_MEMCPY(dst, src, size) do { \
        char* d = static_cast<char*>(dst); \
        const char* s = static_cast<const char*>(src); \
        for (int _i = 0; _i < (size); ++_i) d[_i] = s[_i]; \
    } while(0)
#endif

// ============================================================================
// FREESTANDING TYPE TRAITS (NO <type_traits>)
// ============================================================================

/**
 * @struct sqlite_remove_reference
 * @brief Strips reference qualifiers from a type, equivalent to `std::remove_reference`.
 * @tparam T The input type to transform.
 */
template <typename T> struct sqlite_remove_reference       { typedef T type; };
/** @brief Specialization for lvalue references. */
template <typename T> struct sqlite_remove_reference<T&>  { typedef T type; };
/** @brief Specialization for rvalue references. */
template <typename T> struct sqlite_remove_reference<T&&> { typedef T type; };

/**
 * @struct sqlite_remove_const
 * @brief Strips const qualifiers from a type, equivalent to `std::remove_const`.
 * @tparam T The input type to transform.
 */
template <typename T> struct sqlite_remove_const          { typedef T type; };
/** @brief Specialization for const-qualified types. */
template <typename T> struct sqlite_remove_const<const T> { typedef T type; };

/**
 * @struct sqlite_remove_cv
 * @brief Strips both const and reference qualifiers from a type.
 * @tparam T The input type to transform.
 */
template <typename T>
struct sqlite_remove_cv {
    typedef typename sqlite_remove_const<typename sqlite_remove_reference<T>::type>::type type;
};

/**
 * @struct sqlite_add_rvalue_reference
 * @brief Adds an rvalue reference to a type, equivalent to `std::add_rvalue_reference`.
 * @tparam T The input type to transform.
 */
template <typename T> struct sqlite_add_rvalue_reference       { typedef T&& type; };
/** @brief Specialization for lvalue reference preservation. */
template <typename T> struct sqlite_add_rvalue_reference<T&>  { typedef T&  type; };
/** @brief Specialization for void. */
template <>           struct sqlite_add_rvalue_reference<void> { typedef void type; };

/**
 * @brief Converts any type to a reference type usable in `decltype` expressions without constructing an object.
 *
 * Equivalent to `std::declval<T>()`. Declared but intentionally never defined;
 * solely used within unevaluated contexts (`decltype`, `sizeof`).
 *
 * @tparam T The type to convert.
 * @return An rvalue reference to type T.
 */
template <typename T>
typename sqlite_add_rvalue_reference<T>::type sqlite_declval() noexcept;

/**
 * @struct sqlite_is_trivially_copyable
 * @brief Detects whether a type is trivially copyable using compiler intrinsics.
 * 
 * Directly leverages compiler built-ins (`__is_trivially_copyable`) without standard library
 * `<type_traits>`, allowing containers to safely optimize C++ object copying to raw `memcpy`
 * or `realloc` when mathematically proven safe by the compiler.
 *
 * @tparam T The type to query.
 */
template <typename T>
struct sqlite_is_trivially_copyable {
    static const bool value = __is_trivially_copyable(T);
};

// ============================================================================
// MOVE SEMANTICS & PERFECT FORWARDING (NO <utility>)
// ============================================================================

/**
 * @brief Performs a zero-dependency move cast, equivalent to `std::move`.
 * 
 * Casts an lvalue to an rvalue reference, enabling the compiler to invoke move constructors
 * and move assignment operators without linking to the standard library `<utility>`.
 * 
 * @tparam T Type of the object being moved.
 * @param arg The lvalue or rvalue object reference.
 * @return An rvalue reference to `arg`.
 */
template <typename T>
inline typename sqlite_remove_reference<T>::type&& sqlite_move(T&& arg) noexcept {
    return static_cast<typename sqlite_remove_reference<T>::type&&>(arg);
}

/**
 * @brief Move cast helper specifically named for pointer/handle transfers.
 * @tparam T Type of the pointer or object being moved.
 * @param arg The reference to transfer.
 * @return An rvalue reference to `arg`.
 */
template <typename T>
inline typename sqlite_remove_reference<T>::type&& sqlite_move_ptr(T&& arg) noexcept {
    return static_cast<typename sqlite_remove_reference<T>::type&&>(arg);
}

/**
 * @brief Perfect forwarding cast for lvalues, equivalent to `std::forward<T>`.
 * 
 * Preserves the value category (lvalue or rvalue) of arguments when passing them
 * into downstream functions or constructor delegates.
 * 
 * @tparam T Explicit target type category.
 * @param arg The lvalue argument to forward.
 * @return Perfectly forwarded reference (`T&&`).
 */
template <typename T>
inline T&& sqlite_forward(typename sqlite_remove_reference<T>::type& arg) noexcept {
    return static_cast<T&&>(arg);
}

/**
 * @brief Perfect forwarding cast for rvalues, equivalent to `std::forward<T>`.
 * 
 * @tparam T Explicit target type category.
 * @param arg The rvalue argument to forward.
 * @return Perfectly forwarded reference (`T&&`).
 */
template <typename T>
inline T&& sqlite_forward(typename sqlite_remove_reference<T>::type&& arg) noexcept {
    return static_cast<T&&>(arg);
}

// ============================================================================
// FREESTANDING PLACEMENT NEW & IN-PLACE CONSTRUCTORS (NO <new>)
// ============================================================================

/**
 * @struct sqlite_new_tag
 * @brief Proprietary tag struct used exclusively to disambiguate custom placement-new.
 */
struct sqlite_new_tag {};

/**
 * @brief Custom placement-new operator for zero-dependency in-place constructor invocation.
 * 
 * By defining this globally with our proprietary tag (`sqlite_new_tag`), we instruct the
 * C++ compiler how to evaluate `new (ptr, tag) Type()` syntax natively. This completely
 * bypasses the standard library `<new>` header, preventing linker errors in strict
 * `-nostdlib++` or `-fno-exceptions` environments.
 * 
 * @param p Pre-allocated raw memory address (e.g., from `sqlite3_malloc`).
 * @return The exact same pointer `p`, ready for the compiler to invoke the constructor on.
 */
inline void* operator new(size_t, void* p, sqlite_new_tag) noexcept {
    return p;
}

/**
 * @brief Constructs an object of type `T` in-place at the specified memory address.
 *
 * Hides the boilerplate of the custom placement-new tag. Equivalent to C++20 `std::construct_at`.
 *
 * @tparam T Object type to instantiate.
 * @tparam Args Constructor argument types.
 * @param p Pointer to pre-allocated memory buffer of at least `sizeof(T)` bytes.
 * @param args Forwarded constructor arguments.
 * @return Pointer to the newly constructed object of type `T*`.
 */
template <typename T, typename... Args>
inline T* sqlite_construct_at(T* p, Args&&... args) {
    return new (p, sqlite_new_tag{}) T(sqlite_forward<Args>(args)...);
}

/**
 * @brief Allocates heap memory via `sqlite3_malloc` and constructs an object of type `T` in-place.
 *
 * Mimics standard C++ `new T(args...)`, but guarantees allocation tracking inside SQLite's
 * memory profiler (`sqlite3_memory_used()`). Returns `nullptr` if memory allocation fails.
 *
 * @tparam T Object type to allocate and construct.
 * @tparam Args Constructor argument types.
 * @param args Arguments forwarded to `T`'s constructor.
 * @return Pointer to the newly allocated object, or `nullptr` on allocation failure.
 */
template <typename T, typename... Args>
inline T* sqlite_new(Args&&... args) {
    void* mem = sqlite3_malloc(sizeof(T));
    if (!mem) return nullptr;
    return sqlite_construct_at(static_cast<T*>(mem), sqlite_forward<Args>(args)...);
}

/**
 * @brief Destroys an object and frees its heap memory using `sqlite3_free`.
 *
 * Mimics standard C++ `delete ptr`. Explicitly invokes `ptr->~T()` before releasing memory.
 * Safe to call with `nullptr` (no-op).
 *
 * @tparam T Object type.
 * @param ptr Pointer to the object to destroy and free.
 */
template <typename T>
inline void sqlite_delete(T* ptr) {
    if (ptr) {
        ptr->T::~T();
        sqlite3_free(ptr);
    }
}

/**
 * @brief Invokes the destructor of an object in-place without releasing its memory buffer.
 *
 * Equivalent to C++20 `std::destroy_at`. Safe to call with `nullptr` (no-op).
 *
 * @tparam T Object type.
 * @param p Pointer to the object whose destructor will be executed.
 */
template <typename T>
inline void sqlite_destroy_at(T* p) noexcept {
    if (p) {
        p->T::~T();
    }
}

/**
 * @brief Invokes destructors for `count` elements in a contiguous buffer in reverse order.
 *
 * Equivalent to C++20 `std::destroy_n`. Conforms to the standard C++ guarantee that array
 * elements are destroyed in reverse order of construction.
 *
 * @tparam T Element object type.
 * @param first Pointer to the first element in the contiguous buffer.
 * @param count Number of elements to destroy.
 */
template <typename T>
inline void sqlite_destroy_n(T* first, size_t count) noexcept {
    if (!first) return;
    for (size_t i = count; i > 0; --i) {
        sqlite_destroy_at(&first[i - 1]);
    }
}

/**
 * @brief Allocates raw uninitialized memory for an array of `count` objects via `sqlite3_malloc64`.
 *
 * Includes integer multiplication overflow protection (`count * sizeof(T)`).
 * Note: Does NOT invoke constructors; memory is uninitialized.
 * 
 * @tparam T Element type.
 * @param count Number of elements to allocate space for.
 * @return Pointer to the allocated memory buffer, or `nullptr` on allocation failure / overflow.
 */
template <typename T>
inline T* sqlite_new_array(size_t count) {
    if (count == 0) return nullptr;
    // Check for size_t multiplication overflow:
    if (count > static_cast<size_t>(-1) / sizeof(T)) {
        return nullptr; // Out of memory / overflow prevented
    }
    return static_cast<T*>(sqlite3_malloc64(sizeof(T) * count));
}

/**
 * @brief Reallocates raw memory for an array of `new_count` objects via `sqlite3_realloc64`.
 *
 * Includes integer multiplication overflow protection (`new_count * sizeof(T)`).
 *
 * Operational Behavior:
 * - If `arr` is `nullptr`, behaves identically to `sqlite_new_array<T>(new_count)`.
 * - If `new_count` is 0, frees `arr` via `sqlite3_free` and returns `nullptr`.
 * - Does NOT invoke C++ constructors or destructors for relocated/appended elements.
 * - If reallocation fails, the original memory buffer `arr` remains valid and unmodified in SQLite.
 *
 * @tparam T Element type.
 * @param arr Pointer to the existing array memory buffer (or `nullptr`).
 * @param new_count The new number of elements to reallocate the memory buffer to.
 * @return Pointer to the reallocated memory buffer, or `nullptr` on allocation failure / overflow / zero count.
 */
template <typename T>
inline T* sqlite_reallocate_array(T* arr, size_t new_count) {
    if (new_count == 0) {
        if (arr) sqlite3_free(arr);
        return nullptr;
    }
    // Check for size_t multiplication overflow:
    if (new_count > static_cast<size_t>(-1) / sizeof(T)) {
        return nullptr; // Out of memory / overflow prevented
    }
    return static_cast<T*>(sqlite3_realloc64(arr, sizeof(T) * new_count));
}

/**
 * @brief Frees raw array memory previously allocated via `sqlite_new_array` or `sqlite_reallocate_array`.
 *
 * Note: Does NOT invoke C++ destructors. Call `sqlite_destroy_n` prior to this if `T` is non-trivial.
 * Safe to call with `nullptr` (no-op).
 * 
 * @tparam T Element type.
 * @param arr Pointer to the array memory buffer to release.
 */
template <typename T>
inline void sqlite_delete_array(T* arr) {
    if (arr) {
        sqlite3_free(arr);
    }
}

#endif // SQLITE3_ALLOCATOR_HPP
