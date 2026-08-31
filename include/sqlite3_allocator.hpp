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
#include <string.h>

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
    #define SQLITE_FAST_MEMCPY(dst, src, size) __movsb(reinterpret_cast<unsigned char*>(dst), reinterpret_cast<const unsigned char*>(src), static_cast<size_t>(size))
#else
    #define SQLITE_FAST_MEMCPY(dst, src, size) do { \
        char* d = reinterpret_cast<char*>(dst); \
        const char* s = reinterpret_cast<const char*>(src); \
        for (size_t _i = 0; _i < static_cast<size_t>(size); ++_i) d[_i] = s[_i]; \
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

/**
 * @struct sqlite_is_same
 * @brief Evaluates whether two types are identical, equivalent to `std::is_same`.
 */
template <typename T, typename U> struct sqlite_is_same       { static const bool value = false; };
template <typename T>             struct sqlite_is_same<T, T> { static const bool value = true; };

/**
 * @struct sqlite_is_pointer
 * @brief Compile-time trait to detect if a type is a pointer.
 */
template <typename T> struct sqlite_is_pointer      { static const bool value = false; };
template <typename T> struct sqlite_is_pointer<T*>  { static const bool value = true; };

/**
 * @struct sqlite_enable_if
 * @brief SFINAE conditional type enabler, equivalent to `std::enable_if`.
 */
template <bool B, typename T = void> struct sqlite_enable_if {};
template <typename T> struct sqlite_enable_if<true, T> { typedef T type; };

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
 * @brief Default-constructs `count` elements in a contiguous buffer in forward order.
 *
 * Equivalent to C++20 `std::uninitialized_default_construct_n`. Safe to call with `nullptr` or count 0 (no-op).
 *
 * @tparam T Element object type.
 * @param first Pointer to the first element in the contiguous buffer.
 * @param count Number of elements to construct.
 */
template <typename T>
inline void sqlite_construct_n(T* first, size_t count) {
    if (!first) return;
    for (size_t i = 0; i < count; ++i) {
        sqlite_construct_at(&first[i]);
    }
}

/**
 * @brief In-place constructs `count` elements in a contiguous buffer with forwarded arguments.
 *
 * @tparam T Element object type.
 * @tparam Args Constructor argument types.
 * @param first Pointer to the first element in the contiguous buffer.
 * @param count Number of elements to construct.
 * @param args Arguments forwarded to each element's constructor.
 */
template <typename T, typename... Args>
inline void sqlite_construct_n(T* first, size_t count, Args&&... args) {
    if (!first) return;
    for (size_t i = 0; i < count; ++i) {
        sqlite_construct_at(&first[i], sqlite_forward<Args>(args)...);
    }
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
 * @brief Allocates raw zero-initialized heap memory for an object of type `T` via `sqlite3_malloc` and `memset`.
 *
 * Note: Does NOT invoke constructors; memory is strictly zero-initialized raw bytes.
 *
 * @tparam T Object type to allocate and zero.
 * @return Pointer to the zero-initialized memory buffer, or `nullptr` on allocation failure.
 */
template <typename T>
inline T* sqlite_new_zeroed() {
    void* mem = sqlite3_malloc(sizeof(T));
    if (!mem) return nullptr;
    memset(mem, 0, sizeof(T));
    return static_cast<T*>(mem);
}

/**
 * @brief Allocates raw zero-initialized byte buffer via `sqlite3_malloc64` and `memset`.
 * 
 * @param bytes Total number of bytes to allocate.
 * @return Pointer to allocated zero-initialized memory, or `nullptr` on failure.
 */
inline void* sqlite_malloc_zeroed(size_t bytes) {
    if (bytes == 0) return nullptr;
    void* mem = sqlite3_malloc64(bytes);
    if (mem) {
        memset(mem, 0, bytes);
    }
    return mem;
}

/**
 * @brief Reallocates raw byte buffer via `sqlite3_realloc64`, zero-initializing newly expanded bytes.
 * 
 * @param ptr Pointer to existing memory (or `nullptr`).
 * @param old_bytes Previous byte capacity.
 * @param new_bytes Target new byte capacity.
 * @return Pointer to reallocated buffer, or `nullptr` on failure.
 */
inline void* sqlite_realloc_zeroed(void* ptr, size_t old_bytes, size_t new_bytes) {
    if (new_bytes == 0) {
        if (ptr) sqlite3_free(ptr);
        return nullptr;
    }
    void* new_ptr = sqlite3_realloc64(ptr, new_bytes);
    if (new_ptr && new_bytes > old_bytes) {
        memset(static_cast<unsigned char*>(new_ptr) + old_bytes, 0, new_bytes - old_bytes);
    }
    return new_ptr;
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
 * @brief Allocates raw zero-initialized memory for an array of `count` objects via `sqlite3_malloc64` and `memset`.
 *
 * Includes integer multiplication overflow protection (`count * sizeof(T)`).
 * Note: Zeroes all allocated bytes, but does NOT invoke C++ constructors.
 * 
 * @tparam T Element type.
 * @param count Number of elements to allocate space for.
 * @return Pointer to the zero-initialized memory buffer, or `nullptr` on allocation failure / overflow.
 */
template <typename T>
inline T* sqlite_new_array_zeroed(size_t count) {
    if (count == 0) return nullptr;
    // Check for size_t multiplication overflow:
    if (count > static_cast<size_t>(-1) / sizeof(T)) {
        return nullptr; // Out of memory / overflow prevented
    }
    size_t total_bytes = sizeof(T) * count;
    void* mem = sqlite3_malloc64(total_bytes);
    if (mem) {
        memset(mem, 0, total_bytes);
    }
    return static_cast<T*>(mem);
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
 * @brief Reallocates raw memory for an array of `new_count` objects via `sqlite3_realloc64`, zero-initializing new capacity.
 *
 * Includes integer multiplication overflow protection (`new_count * sizeof(T)`).
 * If `new_count > old_count`, newly appended bytes from `old_count` to `new_count` are guaranteed zeroed via `memset`.
 *
 * @tparam T Element type.
 * @param arr Pointer to the existing array memory buffer (or `nullptr`).
 * @param old_count The previous number of elements allocated in `arr`.
 * @param new_count The new number of elements to reallocate the memory buffer to.
 * @return Pointer to the reallocated memory buffer, or `nullptr` on allocation failure / overflow / zero count.
 */
template <typename T>
inline T* sqlite_reallocate_array_zeroed(T* arr, size_t old_count, size_t new_count) {
    if (new_count == 0) {
        if (arr) sqlite3_free(arr);
        return nullptr;
    }
    // Check for size_t multiplication overflow:
    if (new_count > static_cast<size_t>(-1) / sizeof(T)) {
        return nullptr; // Out of memory / overflow prevented
    }
    T* new_arr = static_cast<T*>(sqlite3_realloc64(arr, sizeof(T) * new_count));
    if (new_arr && new_count > old_count) {
        memset(static_cast<void*>(new_arr + old_count), 0, (new_count - old_count) * sizeof(T));
    }
    return new_arr;
}

/**
 * @brief Frees raw array memory previously allocated via `sqlite_new_array`, `sqlite_new_array_zeroed`, or `sqlite_reallocate_array`.
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

// ============================================================================
// STL-COMPLIANT STATELESS ALLOCATOR FOR CONTAINER INTEGRATION
// ============================================================================

/**
 * @class SqliteAllocator
 * @brief Standard STL-compliant stateless allocator that routes all memory requests
 *        strictly to SQLite's native profiler (sqlite3_malloc64 / sqlite3_free).
 *
 * Conforms to standard C++ allocator requirements, enabling zero-overhead integration
 * with standard and third-party containers (such as GTL flat_hash_map, btree_map, etc.)
 * while maintaining 100% memory accounting in sqlite3_memory_used().
 *
 * @tparam T The element type to allocate.
 */
template <typename T>
class SqliteAllocator {
public:
    using value_type      = T;
    using pointer         = T*;
    using const_pointer   = const T*;
    using reference       = T&;
    using const_reference = const T&;
    using size_type       = size_t;
    using difference_type = ptrdiff_t;

    template <typename U>
    struct rebind {
        using other = SqliteAllocator<U>;
    };

    inline SqliteAllocator() noexcept = default;

    template <typename U>
    inline SqliteAllocator(const SqliteAllocator<U>&) noexcept {}

    /**
     * @brief Allocates raw memory for `n` elements of type `T` via sqlite3_malloc64.
     */
    inline T* allocate(size_t n) {
        return sqlite_new_array<T>(n);
    }

    /**
     * @brief Allocates raw zero-initialized memory for `n` elements of type `T` via sqlite3_malloc64 and memset.
     */
    inline T* allocate_zeroed(size_t n) {
        return sqlite_new_array_zeroed<T>(n);
    }

    /**
     * @brief Frees memory previously allocated with allocate() via sqlite3_free.
     */
    inline void deallocate(T* p, size_t) noexcept {
        sqlite_delete_array(p);
    }

    /**
     * @brief Constructs an element of type `U` in-place at address `p`.
     */
    template <typename U, typename... Args>
    inline void construct(U* p, Args&&... args) {
        sqlite_construct_at(p, sqlite_forward<Args>(args)...);
    }

    /**
     * @brief Destroys an element of type `U` in-place at address `p`.
     */
    template <typename U>
    inline void destroy(U* p) noexcept {
        sqlite_destroy_at(p);
    }

    inline bool operator==(const SqliteAllocator&) const noexcept { return true; }
    inline bool operator!=(const SqliteAllocator&) const noexcept { return false; }
};

#endif // SQLITE3_ALLOCATOR_HPP
