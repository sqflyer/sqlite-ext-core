#ifndef SQLITE3_ALLOCATOR_HPP
#define SQLITE3_ALLOCATOR_HPP

/**
 * @file sqlite3_allocator.hpp
 * @brief Zero-dependency, freestanding memory management & type traits for
 * SQLite extensions.
 *
 * Provides a complete replacement for standard C++ memory allocation (`<new>`,
 * `std::allocator`), move semantics (`<utility>`, `std::move`, `std::forward`),
 * and fundamental type traits
 * (`<type_traits>`). All dynamic memory operations route strictly through
 * SQLite's internal memory profiler (`sqlite3_malloc`, `sqlite3_malloc64`,
 * `sqlite3_free`), ensuring full visibility in `sqlite3_memory_used()` and
 * total independence from the standard C++ runtime
 * (`-nostdlib++` / `/NODEFAULTLIB`).
 */

#include <sqlite3.h>
#include <stddef.h>
#include <string.h>

// ============================================================================
// SQLite Subtype Registry & Function Flags
// ============================================================================
#ifndef SQLITE_SUBTYPE_NONE
    #define SQLITE_SUBTYPE_NONE       0     // 0x00 : Standard untagged SQL value
    #define SQLITE_SUBTYPE_JSON       74    // 'J'  : Official SQLite JSON & JSONB (SQLite 3.45+)
    #define SQLITE_SUBTYPE_DECIMAL    68    // 'D'  : Official SQLite decimal.c extension
    #define SQLITE_SUBTYPE_UUID       85    // 'U'  : Standard 16-byte UUID binary/string
    #define SQLITE_SUBTYPE_VECTOR     86    // 'V'  : AI Embedding Vector (float32/int8)
    #define SQLITE_SUBTYPE_GEOMETRY   71    // 'G'  : Geopoly & GeoJSON spatial coordinate array
    #define SQLITE_SUBTYPE_DATETIME   84    // 'T'  : ISO-8601 & High-precision timestamp
    #define SQLITE_SUBTYPE_BOOL       66    // 'B'  : Explicit Boolean flag (0 or 1)
    #define SQLITE_SUBTYPE_COMPRESSED 90    // 'Z'  : Compressed stream (Gorilla / ZSTD)
    #define SQLITE_SUBTYPE_POINTER    112   // 'p'  : Native SQLite opaque C/C++ typed pointer
#endif

#ifndef SQLITE_SUBTYPE
    #define SQLITE_SUBTYPE 0x00010000
#endif
#ifndef SQLITE_RESULT_SUBTYPE
    #define SQLITE_RESULT_SUBTYPE 0x00100000
#endif

// ============================================================================
// FAST MEMORY COPY MACRO
// ============================================================================

/**
 * @def SQLITE_FAST_MEMCPY
 * @brief Cross-platform, compiler-optimized memory copy intrinsic.
 *
 * Expands to `__builtin_memcpy` on GCC/Clang and `__movsb` on MSVC to guarantee
 * inline assembly code generation without linking against standard C library
 * `memcpy`.
 */
#if defined(__GNUC__) || defined(__clang__)
#define SQLITE_FAST_MEMCPY(dst, src, size)                                     \
  __builtin_memcpy((dst), (src), (size))
#elif defined(_MSC_VER)
#include <intrin.h>
#define SQLITE_FAST_MEMCPY(dst, src, size)                                     \
  __movsb(reinterpret_cast<unsigned char *>(dst),                              \
          reinterpret_cast<const unsigned char *>(src),                        \
          static_cast<size_t>(size))
#else
#define SQLITE_FAST_MEMCPY(dst, src, size)                                     \
  do {                                                                         \
    char *d = reinterpret_cast<char *>(dst);                                   \
    const char *s = reinterpret_cast<const char *>(src);                       \
    for (size_t _i = 0; _i < static_cast<size_t>(size); ++_i)                  \
      d[_i] = s[_i];                                                           \
  } while (0)
#endif

// ============================================================================
// FREESTANDING THREAD-LOCAL STORAGE INTRINSIC (NO <thread>)
// ============================================================================

/**
 * @def SQLITE_THREAD_LOCAL
 * @brief Cross-platform, compiler-native thread-local storage specifier.
 *
 * Bypasses standard C++ `<thread>` runtime library dependencies by utilizing
 * low-level compiler keywords (`__thread` on GCC/Clang and `__declspec(thread)`
 * on MSVC). Enables thread-safe scratch buffers and mutable dummy sinks in
 * strict freestanding `-nostdlib++` environments.
 */
#if defined(_MSC_VER)
#define SQLITE_THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#define SQLITE_THREAD_LOCAL __thread
#elif defined(__cplusplus) && __cplusplus >= 201103L
#define SQLITE_THREAD_LOCAL thread_local
#else
#define SQLITE_THREAD_LOCAL /* Fallback for single-threaded / bare-metal       \
                               targets */
#endif

// ============================================================================
// FREESTANDING HARDWARE DEBUG TRAP INTRINSIC (NO <cassert>)
// ============================================================================

/**
 * @def SQLITE_DEBUG_TRAP
 * @brief Cross-platform hardware breakpoint and execution trap intrinsic.
 *
 * Halts CPU execution at the exact instruction in a debugger without linking
 * against standard C `<assert.h>` or C++ `<cassert>`. Expands to
 * `__builtin_trap()` on GCC/Clang and `__debugbreak()` on MSVC.
 */
#if defined(__GNUC__) || defined(__clang__)
#define SQLITE_DEBUG_TRAP() __builtin_trap()
#elif defined(_MSC_VER)
#include <intrin.h>
#define SQLITE_DEBUG_TRAP() __debugbreak()
#else
#define SQLITE_DEBUG_TRAP()                                                    \
  do {                                                                         \
    (*(volatile int *)0 = 0);                                                  \
  } while (0)
#endif

// ============================================================================
// FREESTANDING TYPE TRAITS (NO <type_traits>)
// ============================================================================

/**
 * @struct sqlite_remove_reference
 * @brief Strips reference qualifiers from a type, equivalent to
 * `std::remove_reference`.
 * @tparam T The input type to transform.
 */
template <typename T> struct sqlite_remove_reference {
  typedef T type;
};
/** @brief Specialization for lvalue references. */
template <typename T> struct sqlite_remove_reference<T &> {
  typedef T type;
};
/** @brief Specialization for rvalue references. */
template <typename T> struct sqlite_remove_reference<T &&> {
  typedef T type;
};

/**
 * @struct sqlite_remove_const
 * @brief Strips const qualifiers from a type, equivalent to
 * `std::remove_const`.
 * @tparam T The input type to transform.
 */
template <typename T> struct sqlite_remove_const {
  typedef T type;
};
/** @brief Specialization for const-qualified types. */
template <typename T> struct sqlite_remove_const<const T> {
  typedef T type;
};

/**
 * @struct sqlite_remove_cv
 * @brief Strips both const and reference qualifiers from a type.
 * @tparam T The input type to transform.
 */
template <typename T> struct sqlite_remove_cv {
  typedef typename sqlite_remove_const<
      typename sqlite_remove_reference<T>::type>::type type;
};

/**
 * @struct sqlite_add_rvalue_reference
 * @brief Adds an rvalue reference to a type, equivalent to
 * `std::add_rvalue_reference`.
 * @tparam T The input type to transform.
 */
template <typename T> struct sqlite_add_rvalue_reference {
  typedef T &&type;
};
/** @brief Specialization for lvalue reference preservation. */
template <typename T> struct sqlite_add_rvalue_reference<T &> {
  typedef T &type;
};
/** @brief Specialization for void. */
template <> struct sqlite_add_rvalue_reference<void> {
  typedef void type;
};

/**
 * @brief Converts any type to a reference type usable in `decltype` expressions
 * without constructing an object.
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
 * @brief Detects whether a type is trivially copyable using compiler
 * intrinsics.
 *
 * Directly leverages compiler built-ins (`__is_trivially_copyable`) without
 * standard library
 * `<type_traits>`, allowing containers to safely optimize C++ object copying to
 * raw `memcpy` or `realloc` when mathematically proven safe by the compiler.
 *
 * @tparam T The type to query.
 */
template <typename T> struct sqlite_is_trivially_copyable {
  static const bool value = __is_trivially_copyable(T);
};

/**
 * @struct sqlite_is_same
 * @brief Evaluates whether two types are identical, equivalent to
 * `std::is_same`.
 */
template <typename T, typename U> struct sqlite_is_same {
  static const bool value = false;
};
template <typename T> struct sqlite_is_same<T, T> {
  static const bool value = true;
};

/**
 * @struct sqlite_is_pointer
 * @brief Compile-time trait to detect if a type is a pointer.
 */
template <typename T> struct sqlite_is_pointer {
  static const bool value = false;
};
template <typename T> struct sqlite_is_pointer<T *> {
  static const bool value = true;
};

/**
 * @struct sqlite_enable_if
 * @brief SFINAE conditional type enabler, equivalent to `std::enable_if`.
 */
template <bool B, typename T = void> struct sqlite_enable_if {};
template <typename T> struct sqlite_enable_if<true, T> {
  typedef T type;
};

// ============================================================================
// MOVE SEMANTICS & PERFECT FORWARDING (NO <utility>)
// ============================================================================

/**
 * @brief Performs a zero-dependency move cast, equivalent to `std::move`.
 *
 * Casts an lvalue to an rvalue reference, enabling the compiler to invoke move
 * constructors and move assignment operators without linking to the standard
 * library `<utility>`.
 *
 * @tparam T Type of the object being moved.
 * @param arg The lvalue or rvalue object reference.
 * @return An rvalue reference to `arg`.
 */
template <typename T>
inline typename sqlite_remove_reference<T>::type &&
sqlite_move(T &&arg) noexcept {
  return static_cast<typename sqlite_remove_reference<T>::type &&>(arg);
}

/**
 * @brief Move cast helper specifically named for pointer/handle transfers.
 * @tparam T Type of the pointer or object being moved.
 * @param arg The reference to transfer.
 * @return An rvalue reference to `arg`.
 */
template <typename T>
inline typename sqlite_remove_reference<T>::type &&
sqlite_move_ptr(T &&arg) noexcept {
  return static_cast<typename sqlite_remove_reference<T>::type &&>(arg);
}

/**
 * @brief Perfect forwarding cast for lvalues, equivalent to `std::forward<T>`.
 *
 * Preserves the value category (lvalue or rvalue) of arguments when passing
 * them into downstream functions or constructor delegates.
 *
 * @tparam T Explicit target type category.
 * @param arg The lvalue argument to forward.
 * @return Perfectly forwarded reference (`T&&`).
 */
template <typename T>
inline T &&
sqlite_forward(typename sqlite_remove_reference<T>::type &arg) noexcept {
  return static_cast<T &&>(arg);
}

/**
 * @brief Perfect forwarding cast for rvalues, equivalent to `std::forward<T>`.
 *
 * @tparam T Explicit target type category.
 * @param arg The rvalue argument to forward.
 * @return Perfectly forwarded reference (`T&&`).
 */
template <typename T>
inline T &&
sqlite_forward(typename sqlite_remove_reference<T>::type &&arg) noexcept {
  return static_cast<T &&>(arg);
}

// ============================================================================
// FREESTANDING PLACEMENT NEW & IN-PLACE CONSTRUCTORS (NO <new>)
// ============================================================================

/**
 * @struct sqlite_new_tag
 * @brief Proprietary tag struct used exclusively to disambiguate custom
 * placement-new.
 */
struct sqlite_new_tag {};

/**
 * @brief Custom placement-new operator for zero-dependency in-place constructor
 * invocation.
 *
 * By defining this globally with our proprietary tag (`sqlite_new_tag`), we
 * instruct the C++ compiler how to evaluate `new (ptr, tag) Type()` syntax
 * natively. This completely bypasses the standard library `<new>` header,
 * preventing linker errors in strict
 * `-nostdlib++` or `-fno-exceptions` environments.
 *
 * @param p Pre-allocated raw memory address (e.g., from `sqlite3_malloc`).
 * @return The exact same pointer `p`, ready for the compiler to invoke the
 * constructor on.
 */
inline void *operator new(size_t, void *p, sqlite_new_tag) noexcept {
  return p;
}

/**
 * @brief Constructs an object of type `T` in-place at the specified memory
 * address.
 *
 * Hides the boilerplate of the custom placement-new tag. Equivalent to C++20
 * `std::construct_at`.
 *
 * @tparam T Object type to instantiate.
 * @tparam Args Constructor argument types.
 * @param p Pointer to pre-allocated memory buffer of at least `sizeof(T)`
 * bytes.
 * @param args Forwarded constructor arguments.
 * @return Pointer to the newly constructed object of type `T*`.
 */
template <typename T, typename... Args>
inline T *sqlite_construct_at(T *p, Args &&...args) {
  return new (p, sqlite_new_tag{}) T(sqlite_forward<Args>(args)...);
}

/**
 * @brief Default-constructs `count` elements in a contiguous buffer in forward
 * order.
 *
 * Equivalent to C++20 `std::uninitialized_default_construct_n`. Safe to call
 * with `nullptr` or count 0 (no-op).
 *
 * @tparam T Element object type.
 * @param first Pointer to the first element in the contiguous buffer.
 * @param count Number of elements to construct.
 */
template <typename T> inline void sqlite_construct_n(T *first, size_t count) {
  if (!first)
    return;
  for (size_t i = 0; i < count; ++i) {
    sqlite_construct_at(&first[i]);
  }
}

/**
 * @brief In-place constructs `count` elements in a contiguous buffer with
 * forwarded arguments.
 *
 * @tparam T Element object type.
 * @tparam Args Constructor argument types.
 * @param first Pointer to the first element in the contiguous buffer.
 * @param count Number of elements to construct.
 * @param args Arguments forwarded to each element's constructor.
 */
template <typename T, typename... Args>
inline void sqlite_construct_n(T *first, size_t count, Args &&...args) {
  if (!first)
    return;
  for (size_t i = 0; i < count; ++i) {
    sqlite_construct_at(&first[i], sqlite_forward<Args>(args)...);
  }
}

/**
 * @brief Allocates heap memory via `sqlite3_malloc` and constructs an object of
 * type `T` in-place.
 *
 * Mimics standard C++ `new T(args...)`, but guarantees allocation tracking
 * inside SQLite's memory profiler (`sqlite3_memory_used()`). Returns `nullptr`
 * if memory allocation fails.
 *
 * @tparam T Object type to allocate and construct.
 * @tparam Args Constructor argument types.
 * @param args Arguments forwarded to `T`'s constructor.
 * @return Pointer to the newly allocated object, or `nullptr` on allocation
 * failure.
 */
template <typename T, typename... Args> inline T *sqlite_new(Args &&...args) {
  void *mem = sqlite3_malloc64(sizeof(T));
  if (!mem)
    return nullptr;
  return sqlite_construct_at(static_cast<T *>(mem),
                             sqlite_forward<Args>(args)...);
}

/**
 * @brief Allocates raw zero-initialized heap memory for an object of type `T`
 * via `sqlite3_malloc64` and `memset`.
 *
 * Note: Does NOT invoke constructors; memory is strictly zero-initialized raw
 * bytes.
 *
 * @tparam T Object type to allocate and zero.
 * @return Pointer to the zero-initialized memory buffer, or `nullptr` on
 * allocation failure.
 */
template <typename T> inline T *sqlite_new_zeroed() {
  void *mem = sqlite3_malloc64(sizeof(T));
  if (!mem)
    return nullptr;
  memset(mem, 0, sizeof(T));
  return static_cast<T *>(mem);
}

/**
 * @brief Allocates raw zero-initialized byte buffer via `sqlite3_malloc64` and
 * `memset`.
 *
 * @param bytes Total number of bytes to allocate.
 * @return Pointer to allocated zero-initialized memory, or `nullptr` on
 * failure.
 */
inline void *sqlite_malloc_zeroed(size_t bytes) {
  if (bytes == 0)
    return nullptr;
  void *mem = sqlite3_malloc64(bytes);
  if (mem) {
    memset(mem, 0, bytes);
  }
  return mem;
}

/**
 * @brief Reallocates raw byte buffer via `sqlite3_realloc64`, zero-initializing
 * newly expanded bytes.
 *
 * @param ptr Pointer to existing memory (or `nullptr`).
 * @param old_bytes Previous byte capacity.
 * @param new_bytes Target new byte capacity.
 * @return Pointer to reallocated buffer, or `nullptr` on failure.
 */
inline void *sqlite_realloc_zeroed(void *ptr, size_t old_bytes,
                                   size_t new_bytes) {
  if (new_bytes == 0) {
    if (ptr)
      sqlite3_free(ptr);
    return nullptr;
  }
  void *new_ptr = sqlite3_realloc64(ptr, new_bytes);
  if (new_ptr && new_bytes > old_bytes) {
    memset(static_cast<unsigned char *>(new_ptr) + old_bytes, 0,
           new_bytes - old_bytes);
  }
  return new_ptr;
}

/**
 * @brief Destroys an object and frees its heap memory using `sqlite3_free`.
 *
 * Mimics standard C++ `delete ptr`. Explicitly invokes `ptr->~T()` before
 * releasing memory. Safe to call with `nullptr` (no-op).
 *
 * @tparam T Object type.
 * @param ptr Pointer to the object to destroy and free.
 */
template <typename T> inline void sqlite_delete(T *ptr) {
  if (ptr) {
    ptr->~T();
    sqlite3_free(ptr);
  }
}

/**
 * @brief Invokes the destructor of an object in-place without releasing its
 * memory buffer.
 *
 * Equivalent to C++20 `std::destroy_at`. Safe to call with `nullptr` (no-op).
 *
 * @tparam T Object type.
 * @param p Pointer to the object whose destructor will be executed.
 */
template <typename T> inline void sqlite_destroy_at(T *p) noexcept {
  if (p) {
    p->~T();
  }
}

/**
 * @brief Invokes destructors for `count` elements in a contiguous buffer in
 * reverse order.
 *
 * Equivalent to C++20 `std::destroy_n`. Conforms to the standard C++ guarantee
 * that array elements are destroyed in reverse order of construction.
 *
 * @tparam T Element object type.
 * @param first Pointer to the first element in the contiguous buffer.
 * @param count Number of elements to destroy.
 */
template <typename T>
inline void sqlite_destroy_n(T *first, size_t count) noexcept {
  if (!first)
    return;
  for (size_t i = count; i > 0; --i) {
    sqlite_destroy_at(&first[i - 1]);
  }
}

/**
 * @brief Allocates raw uninitialized memory for an array of `count` objects via
 * `sqlite3_malloc64`.
 *
 * Includes integer multiplication overflow protection (`count * sizeof(T)`).
 * Note: Does NOT invoke constructors; memory is uninitialized.
 *
 * @tparam T Element type.
 * @param count Number of elements to allocate space for.
 * @return Pointer to the allocated memory buffer, or `nullptr` on allocation
 * failure / overflow.
 */
template <typename T> inline T *sqlite_new_array(size_t count) {
  if (count == 0)
    return nullptr;
  // Check for size_t multiplication overflow:
  if (count > static_cast<size_t>(-1) / sizeof(T)) {
    return nullptr; // Out of memory / overflow prevented
  }
  return static_cast<T *>(sqlite3_malloc64(sizeof(T) * count));
}

/**
 * @brief Allocates raw zero-initialized memory for an array of `count` objects
 * via `sqlite3_malloc64` and `memset`.
 *
 * Includes integer multiplication overflow protection (`count * sizeof(T)`).
 * Note: Zeroes all allocated bytes, but does NOT invoke C++ constructors.
 *
 * @tparam T Element type.
 * @param count Number of elements to allocate space for.
 * @return Pointer to the zero-initialized memory buffer, or `nullptr` on
 * allocation failure / overflow.
 */
template <typename T> inline T *sqlite_new_array_zeroed(size_t count) {
  if (count == 0)
    return nullptr;
  // Check for size_t multiplication overflow:
  if (count > static_cast<size_t>(-1) / sizeof(T)) {
    return nullptr; // Out of memory / overflow prevented
  }
  size_t total_bytes = sizeof(T) * count;
  void *mem = sqlite3_malloc64(total_bytes);
  if (mem) {
    memset(mem, 0, total_bytes);
  }
  return static_cast<T *>(mem);
}

/**
 * @brief Reallocates raw memory for an array of `new_count` objects via
 * `sqlite3_realloc64`.
 *
 * Includes integer multiplication overflow protection (`new_count *
 * sizeof(T)`).
 *
 * Operational Behavior:
 * - If `arr` is `nullptr`, behaves identically to
 * `sqlite_new_array<T>(new_count)`.
 * - If `new_count` is 0, frees `arr` via `sqlite3_free` and returns `nullptr`.
 * - Does NOT invoke C++ constructors or destructors for relocated/appended
 * elements.
 * - If reallocation fails, the original memory buffer `arr` remains valid and
 * unmodified in SQLite.
 *
 * @tparam T Element type.
 * @param arr Pointer to the existing array memory buffer (or `nullptr`).
 * @param new_count The new number of elements to reallocate the memory buffer
 * to.
 * @return Pointer to the reallocated memory buffer, or `nullptr` on allocation
 * failure / overflow / zero count.
 */
template <typename T>
inline T *sqlite_reallocate_array(T *arr, size_t new_count) {
  if (new_count == 0) {
    if (arr)
      sqlite3_free(arr);
    return nullptr;
  }
  // Check for size_t multiplication overflow:
  if (new_count > static_cast<size_t>(-1) / sizeof(T)) {
    return nullptr; // Out of memory / overflow prevented
  }
  return static_cast<T *>(sqlite3_realloc64(arr, sizeof(T) * new_count));
}

/**
 * @brief Reallocates raw memory for an array of `new_count` objects via
 * `sqlite3_realloc64`, zero-initializing new capacity.
 *
 * Includes integer multiplication overflow protection (`new_count *
 * sizeof(T)`). If `new_count > old_count`, newly appended bytes from
 * `old_count` to `new_count` are guaranteed zeroed via `memset`.
 *
 * @tparam T Element type.
 * @param arr Pointer to the existing array memory buffer (or `nullptr`).
 * @param old_count The previous number of elements allocated in `arr`.
 * @param new_count The new number of elements to reallocate the memory buffer
 * to.
 * @return Pointer to the reallocated memory buffer, or `nullptr` on allocation
 * failure / overflow / zero count.
 */
template <typename T>
inline T *sqlite_reallocate_array_zeroed(T *arr, size_t old_count,
                                         size_t new_count) {
  if (new_count == 0) {
    if (arr)
      sqlite3_free(arr);
    return nullptr;
  }
  // Check for size_t multiplication overflow:
  if (new_count > static_cast<size_t>(-1) / sizeof(T)) {
    return nullptr; // Out of memory / overflow prevented
  }
  T *new_arr = static_cast<T *>(sqlite3_realloc64(arr, sizeof(T) * new_count));
  if (new_arr && new_count > old_count) {
    memset(static_cast<void *>(new_arr + old_count), 0,
           (new_count - old_count) * sizeof(T));
  }
  return new_arr;
}

/**
 * @brief Frees raw array memory previously allocated via `sqlite_new_array`,
 * `sqlite_new_array_zeroed`, or `sqlite_reallocate_array`.
 *
 * Note: Does NOT invoke C++ destructors. Call `sqlite_destroy_n` prior to this
 * if `T` is non-trivial. Safe to call with `nullptr` (no-op).
 *
 * @tparam T Element type.
 * @param arr Pointer to the array memory buffer to release.
 */
template <typename T> inline void sqlite_delete_array(T *arr) {
  if (arr) {
    sqlite3_free(arr);
  }
}

// ============================================================================
// STL-COMPLIANT STATELESS ALLOCATOR FOR CONTAINER INTEGRATION
// ============================================================================

/**
 * @class SqliteAllocator
 * @brief Standard STL-compliant stateless allocator that routes all memory
 * requests strictly to SQLite's native profiler (sqlite3_malloc64 /
 * sqlite3_free).
 *
 * Conforms to standard C++ allocator requirements, enabling zero-overhead
 * integration with standard and third-party containers (such as GTL
 * flat_hash_map, btree_map, etc.) while maintaining 100% memory accounting in
 * sqlite3_memory_used().
 *
 * All allocations strictly respect `-fno-exceptions` and return `nullptr` upon
 * failure without throwing `std::bad_alloc`.
 *
 * @tparam T The element type to allocate.
 */
template <typename T> class SqliteAllocator {
public:
  using value_type = T;
  using pointer = T *;
  using const_pointer = const T *;
  using reference = T &;
  using const_reference = const T &;
  using size_type = size_t;
  using difference_type = ptrdiff_t;

  /**
   * @struct rebind
   * @brief Enables STL containers to adapt the allocator for internal node
   * types.
   * @tparam U Target element type.
   */
  template <typename U> struct rebind {
    using other = SqliteAllocator<U>;
  };

  /** @brief Default constructor for stateless allocator. */
  inline SqliteAllocator() noexcept = default;

  /**
   * @brief Converting copy constructor for converting between rebound allocator
   * instances.
   * @tparam U Source allocator element type.
   */
  template <typename U>
  inline SqliteAllocator(const SqliteAllocator<U> &) noexcept {}

  /**
   * @brief Allocates raw uninitialized memory for `n` elements of type `T` via
   * `sqlite3_malloc64`.
   *
   * Includes integer overflow protection. Returns `nullptr` if memory is
   * exhausted or if `n == 0`.
   *
   * @param n Number of elements to allocate.
   * @return Pointer to allocated memory, or `nullptr` on allocation failure.
   */
  inline T *allocate(size_t n) { return sqlite_new_array<T>(n); }

  /**
   * @brief Allocates raw zero-initialized memory for `n` elements of type `T`
   * via `sqlite3_malloc64` and `memset`.
   *
   * @param n Number of elements to allocate and zero.
   * @return Pointer to allocated zero-initialized memory, or `nullptr` on
   * allocation failure.
   */
  inline T *allocate_zeroed(size_t n) { return sqlite_new_array_zeroed<T>(n); }

  /**
   * @brief Frees memory previously allocated with `allocate()` or
   * `allocate_zeroed()` via `sqlite3_free`.
   *
   * Safe to invoke with `nullptr` (no-op).
   *
   * @param p Pointer to memory buffer to release.
   */
  inline void deallocate(T *p, size_t) noexcept { sqlite_delete_array(p); }

  /**
   * @brief In-place constructs an element of type `U` at address `p` using
   * perfect forwarding.
   *
   * @tparam U Target element type to construct.
   * @tparam Args Forwarded constructor argument types.
   * @param p Pointer to uninitialized memory buffer of at least `sizeof(U)`
   * bytes.
   * @param args Arguments forwarded to `U`'s constructor.
   */
  template <typename U, typename... Args>
  inline void construct(U *p, Args &&...args) {
    sqlite_construct_at(p, sqlite_forward<Args>(args)...);
  }

  /**
   * @brief In-place destroys an element of type `U` at address `p` without
   * deallocating memory.
   *
   * Safe to invoke with `nullptr` (no-op).
   *
   * @tparam U Target element type to destroy.
   * @param p Pointer to constructed object.
   */
  template <typename U> inline void destroy(U *p) noexcept {
    sqlite_destroy_at(p);
  }

  /** @brief Stateless allocators of the same type are always equal. */
  inline bool operator==(const SqliteAllocator &) const noexcept {
    return true;
  }
  /** @brief Stateless allocators of the same type are never unequal. */
  inline bool operator!=(const SqliteAllocator &) const noexcept {
    return false;
  }
};

// ============================================================================
// GO-STYLE RESULT AND STATUS TYPES WITH CUSTOM ERROR MESSAGES
// ============================================================================

/**
 * @struct SqliteStatus
 * @brief Zero-overhead, C-compatible status struct pairing an integer SQLite
 * return code with an optional custom human-readable error message string.
 *
 * Conforms to Rust-style error conventions where errors are represented
 * explicitly rather than thrown as exceptions.
 */
struct SqliteStatus {
  int code; /**< SQLite result/error code (e.g. SQLITE_OK, SQLITE_NOMEM,
               SQLITE_BUSY). */
  const char *message; /**< Optional custom human-readable error message (or
                          nullptr for default). */

  /** @brief Default constructor initializing to SQLITE_OK (success) with no
   * error message. */
  inline SqliteStatus() noexcept : code(SQLITE_OK), message(nullptr) {}

  /**
   * @brief Explicit constructor initializing code and optional custom error
   * message.
   * @param c SQLite return code.
   * @param msg Optional custom error message string.
   */
  inline SqliteStatus(int c, const char *msg = nullptr) noexcept
      : code(c), message(msg) {}

  /** @brief Returns true if the status represents success (code == SQLITE_OK).
   */
  inline bool is_ok() const noexcept { return code == SQLITE_OK; }

  /** @brief Returns true if the status represents an error (code != SQLITE_OK).
   */
  inline bool is_err() const noexcept { return code != SQLITE_OK; }

  /** @brief Boolean conversion operator, evaluating to true on success
   * (is_ok()). */
  inline explicit operator bool() const noexcept { return is_ok(); }

  /** @brief Returns the integer SQLite error code. */
  inline int err_code() const noexcept { return code; }

  /** @brief Returns the custom error message pointer (or nullptr if none
   * provided). */
  inline const char *err_message() const noexcept { return message; }

  /**
   * @brief Returns the error description string.
   *
   * Returns the custom error message if present, or falls back to SQLite's
   * native `sqlite3_errstr(code)`.
   *
   * @return Non-null C-string describing the status.
   */
  inline const char *err_msg() const noexcept {
    if (message)
      return message;
    return sqlite3_errstr(code);
  }

  /** @brief Returns the error description string (alias for err_msg()). */
  inline const char *msg() const noexcept { return err_msg(); }

  /**
   * @brief Sets SQLite function error on `sqlite3_context` if this status is an
   * error.
   * @return True if error was set, false if status was ok.
   */
  inline bool set_sqlite_err(sqlite3_context *ctx) const noexcept {
    if (is_err()) {
      if (code == SQLITE_NOMEM) {
        sqlite3_result_error_nomem(ctx);
      } else if (code == SQLITE_TOOBIG) {
        sqlite3_result_error_toobig(ctx);
      } else {
        sqlite3_result_error(ctx, err_msg(), -1);
        sqlite3_result_error_code(ctx, code);
      }
      return true;
    }
    return false;
  }

  /** @brief Factory constructor returning a successful status (SQLITE_OK). */
  static inline SqliteStatus ok() noexcept {
    return SqliteStatus(SQLITE_OK, nullptr);
  }

  /**
   * @brief Factory constructor returning an error status with custom message.
   * @param c SQLite error code.
   * @param msg Optional custom error message string.
   */
  static inline SqliteStatus err(int c, const char *msg = nullptr) noexcept {
    return SqliteStatus(c, msg);
  }

  /**
   * @brief Factory constructor returning an out-of-memory error (SQLITE_NOMEM).
   * @param msg Custom out-of-memory message (defaults to "Out of memory").
   */
  static inline SqliteStatus nomem(const char *msg = "Out of memory") noexcept {
    return SqliteStatus(SQLITE_NOMEM, msg);
  }

  /** @brief Equality comparison evaluating whether status codes match. */
  inline bool operator==(const SqliteStatus &other) const noexcept {
    return code == other.code;
  }

  /** @brief Inequality comparison evaluating whether status codes differ. */
  inline bool operator!=(const SqliteStatus &other) const noexcept {
    return code != other.code;
  }

  /** @brief Equality comparison with raw SQLite integer code. */
  inline bool operator==(int c) const noexcept { return code == c; }

  /** @brief Inequality comparison with raw SQLite integer code. */
  inline bool operator!=(int c) const noexcept { return code != c; }
};

/**
 * @struct SqliteResult
 * @brief Rust-style explicit return struct bundling a typed payload `T` with a
 * `SqliteStatus` (SQLite error code and optional custom error message).
 *
 * Enables zero-exception, explicit error handling:
 * @code
 * auto res = sqlite_try_new<MyStruct>(42);
 * if (res.is_err()) {
 *     printf("Allocation failed [%d]: %s\n", res.err_code(),
 * res.err_message()); return res.err_code();
 * }
 * MyStruct* obj = res.unwrap();
 * @endcode
 *
 * @tparam T Payload type.
 */
template <typename T> struct SqliteResult {
  T val;             /**< The encapsulated payload value. */
  SqliteStatus stat; /**< The status metadata (code + optional message). */

  /** @brief Default constructor initializing value to default and status to
   * SQLITE_OK. */
  inline SqliteResult() noexcept : val(), stat(SQLITE_OK, nullptr) {}

  /** @brief Value constructor initializing payload and setting status to
   * SQLITE_OK. */
  inline SqliteResult(T v) : val(sqlite_move(v)), stat(SQLITE_OK, nullptr) {}

  /** @brief Constructor with value and explicit SqliteStatus. */
  inline SqliteResult(T v, SqliteStatus s) : val(sqlite_move(v)), stat(s) {}

  /** @brief Constructor with value, error code, and optional custom message. */
  inline SqliteResult(T v, int code, const char *msg = nullptr)
      : val(sqlite_move(v)), stat(code, msg) {}

  /** @brief Error constructor initializing status from SqliteStatus with
   * default value. */
  inline SqliteResult(SqliteStatus s) noexcept : val(), stat(s) {}

  /** @brief Error constructor initializing error code and optional custom
   * message with default value. */
  inline SqliteResult(int code, const char *msg = nullptr) noexcept
      : val(), stat(code, msg) {}

  /** @brief Converting constructor for convertible payload types (e.g. Derived* to Base*). */
  template <typename U>
  inline SqliteResult(const SqliteResult<U> &other) noexcept
      : val(static_cast<T>(other.val)), stat(other.stat) {}

  /** @brief Converting move constructor for convertible payload types. */
  template <typename U>
  inline SqliteResult(SqliteResult<U> &&other) noexcept
      : val(static_cast<T>(sqlite_move(other.val))), stat(other.stat) {}

  /** @brief Returns true if the result represents success (is_ok()). */
  inline bool is_ok() const noexcept { return stat.is_ok(); }

  /** @brief Returns true if the result represents an error (is_err()). */
  inline bool is_err() const noexcept { return stat.is_err(); }

  /** @brief Boolean conversion operator, evaluating to true on success
   * (is_ok()). */
  inline explicit operator bool() const noexcept { return is_ok(); }

  /** @brief Unwraps and returns a mutable reference to the payload value. */
  inline T &unwrap() & noexcept { return val; }

  /** @brief Unwraps and returns a const reference to the payload value. */
  inline const T &unwrap() const & noexcept { return val; }

  /** @brief Unwraps and moves the payload value out of an rvalue result. */
  inline T unwrap() && noexcept { return sqlite_move(val); }

  /** @brief Moves and returns the payload value out of this result. */
  inline T take_value() noexcept { return sqlite_move(val); }

  /** @brief Returns the payload value if successful, or default_val on error.
   */
  inline T unwrap_or(T default_val) noexcept {
    return is_ok() ? sqlite_move(val) : sqlite_move(default_val);
  }

  /** @brief Returns the payload value if successful, or default-constructed
   * T{} on error. */
  inline T unwrap_or_default() noexcept {
    return is_ok() ? sqlite_move(val) : T();
  }

  /** @brief Lazily computes a fallback value on error via callable
   * f(SqliteStatus). */
  template <typename F> inline T unwrap_or_else(F &&f) {
    return is_ok() ? sqlite_move(val) : f(stat);
  }

  /** @brief Returns the payload value or continues with unwrap value. */
  inline T expect(const char *msg) {
    (void)msg;
    return sqlite_move(val);
  }

  /** @brief Returns a mutable reference to the payload value. */
  inline T &value() & noexcept { return val; }

  /** @brief Returns a const reference to the payload value. */
  inline const T &value() const & noexcept { return val; }

  /** @brief Moves and returns the payload value out of an rvalue result. */
  inline T value() && noexcept { return sqlite_move(val); }

  /** @brief Member access operator pointing to the underlying payload. */
  inline T *operator->() noexcept { return &val; }

  /** @brief Const member access operator pointing to the underlying payload. */
  inline const T *operator->() const noexcept { return &val; }

  /** @brief Dereference operator returning mutable reference to payload. */
  inline T &operator*() & noexcept { return val; }

  /** @brief Dereference operator returning const reference to payload. */
  inline const T &operator*() const & noexcept { return val; }

  /** @brief Dereference operator moving payload out of an rvalue result. */
  inline T &&operator*() && noexcept { return sqlite_move(val); }

  /** @brief Returns the integer SQLite error code. */
  inline int err_code() const noexcept { return stat.err_code(); }

  /** @brief Returns the custom error message pointer (or nullptr if none
   * provided). */
  inline const char *err_message() const noexcept {
    return stat.err_message();
  }

  /** @brief Returns the error message string (custom message or
   * sqlite3_errstr). */
  inline const char *err_msg() const noexcept { return stat.err_msg(); }

  /** @brief Returns the error message string (alias for err_msg()). */
  inline const char *msg() const noexcept { return stat.msg(); }

  /** @brief Returns the encapsulated SqliteStatus metadata. */
  inline SqliteStatus status() const noexcept { return stat; }

  /**
   * @brief Applies transformation `f(val)` if ok; propagates error status
   * otherwise.
   */
  template <typename F>
  inline auto map(F &&f) -> SqliteResult<decltype(f(val))> {
    using U = decltype(f(val));
    if (is_err()) {
      return SqliteResult<U>::err(stat.err_code(), stat.err_message());
    }
    return SqliteResult<U>::ok(f(val));
  }

  /**
   * @brief Applies function `f(val)` returning SqliteResult<U> if ok;
   * propagates error status otherwise.
   */
  template <typename F> inline auto and_then(F &&f) -> decltype(f(val)) {
    if (is_err()) {
      using ResultU = decltype(f(val));
      return ResultU::err(stat.err_code(), stat.err_message());
    }
    return f(val);
  }

  /**
   * @brief Applies fallback function `f(stat)` returning SqliteResult<T> if
   * err; returns self if ok.
   */
  template <typename F> inline SqliteResult<T> or_else(F &&f) {
    if (is_ok()) {
      return sqlite_move(*this);
    }
    return f(stat);
  }

  /**
   * @brief Applies error transformation `f(stat)` returning const char* if err;
   * returns self if ok.
   */
  template <typename F> inline SqliteResult<T> map_err(F &&f) {
    if (is_ok()) {
      return sqlite_move(*this);
    }
    return SqliteResult<T>(stat.err_code(), f(stat));
  }

  /**
   * @brief Invokes side-effect callback `f(val)` if ok, returning const
   * reference to self.
   */
  template <typename F> inline const SqliteResult<T> &inspect(F &&f) const {
    if (is_ok()) {
      f(val);
    }
    return *this;
  }

  /**
   * @brief Invokes side-effect callback `f(stat)` if err, returning const
   * reference to self.
   */
  template <typename F>
  inline const SqliteResult<T> &inspect_err(F &&f) const {
    if (is_err()) {
      f(stat);
    }
    return *this;
  }

  /**
   * @brief Sets SQLite function error on `sqlite3_context` if this result is an
   * error.
   * @return True if error was set, false if result was ok.
   */
  inline bool set_sqlite_err(sqlite3_context *ctx) const noexcept {
    if (is_err()) {
      if (stat.err_code() == SQLITE_NOMEM) {
        sqlite3_result_error_nomem(ctx);
      } else if (stat.err_code() == SQLITE_TOOBIG) {
        sqlite3_result_error_toobig(ctx);
      } else {
        sqlite3_result_error(ctx, stat.err_msg(), -1);
        sqlite3_result_error_code(ctx, stat.err_code());
      }
      return true;
    }
    return false;
  }

  /**
   * @brief Factory constructor returning a successful result containing
   * payload `v`.
   * @param v The payload value to store.
   * @return SqliteResult<T> initialized to SQLITE_OK.
   */
  static inline SqliteResult<T> ok(T v) {
    return SqliteResult<T>(sqlite_move(v), SQLITE_OK, nullptr);
  }

  /**
   * @brief Factory constructor returning an error result with custom message.
   * @param code SQLite error code.
   * @param msg Optional custom error message string.
   * @return SqliteResult<T> initialized with error status.
   */
  static inline SqliteResult<T> err(int code,
                                    const char *msg = nullptr) noexcept {
    return SqliteResult<T>(code, msg);
  }

  /**
   * @brief Factory constructor returning an out-of-memory error (SQLITE_NOMEM).
   * @param msg Optional custom out-of-memory message (defaults to "Out of
   * memory").
   * @return SqliteResult<T> initialized with SQLITE_NOMEM status.
   */
  static inline SqliteResult<T>
  nomem(const char *msg = "Out of memory") noexcept {
    return SqliteResult<T>(SQLITE_NOMEM, msg);
  }
};

/**
 * @struct SqliteResult<void>
 * @brief Specialization of SqliteResult for value-less fallible operations.
 */
template <> struct SqliteResult<void> {
  SqliteStatus stat; /**< The status metadata (code + optional message). */

  /** @brief Default constructor initializing to SQLITE_OK. */
  inline SqliteResult() noexcept : stat(SQLITE_OK, nullptr) {}

  /** @brief Constructor from explicit SqliteStatus. */
  inline SqliteResult(SqliteStatus s) noexcept : stat(s) {}

  /** @brief Constructor from error code and optional custom message. */
  inline SqliteResult(int code, const char *msg = nullptr) noexcept
      : stat(code, msg) {}

  /** @brief Returns true if the result represents success (is_ok()). */
  inline bool is_ok() const noexcept { return stat.is_ok(); }

  /** @brief Returns true if the result represents an error (is_err()). */
  inline bool is_err() const noexcept { return stat.is_err(); }

  /** @brief Boolean conversion operator, evaluating to true on success
   * (is_ok()). */
  inline explicit operator bool() const noexcept { return is_ok(); }

  /** @brief Returns the integer SQLite error code. */
  inline int err_code() const noexcept { return stat.err_code(); }

  /** @brief Returns the custom error message pointer (or nullptr if none
   * provided). */
  inline const char *err_message() const noexcept {
    return stat.err_message();
  }

  /** @brief Returns the error message string (custom message or
   * sqlite3_errstr). */
  inline const char *err_msg() const noexcept { return stat.err_msg(); }

  /** @brief Returns the error message string (alias for err_msg()). */
  inline const char *msg() const noexcept { return stat.msg(); }

  /** @brief Returns the encapsulated SqliteStatus metadata. */
  inline SqliteStatus status() const noexcept { return stat; }

  /**
   * @brief Invokes side-effect callback `f(stat)` if err, returning const
   * reference to self.
   */
  template <typename F>
  inline const SqliteResult<void> &inspect_err(F &&f) const {
    if (is_err()) {
      f(stat);
    }
    return *this;
  }

  /**
   * @brief Applies fallback function `f(stat)` returning SqliteResult<void> if
   * err; returns self if ok.
   */
  template <typename F> inline SqliteResult<void> or_else(F &&f) {
    if (is_ok()) {
      return *this;
    }
    return f(stat);
  }

  /**
   * @brief Applies function `f()` returning SqliteResult<U> if ok; propagates
   * error status otherwise.
   */
  template <typename F> inline auto and_then(F &&f) -> decltype(f()) {
    if (is_err()) {
      using ResultU = decltype(f());
      return ResultU::err(stat.err_code(), stat.err_message());
    }
    return f();
  }

  /**
   * @brief Sets SQLite function error on `sqlite3_context` if this result is an
   * error.
   * @return True if error was set, false if result was ok.
   */
  inline bool set_sqlite_err(sqlite3_context *ctx) const noexcept {
    if (is_err()) {
      if (stat.err_code() == SQLITE_NOMEM) {
        sqlite3_result_error_nomem(ctx);
      } else if (stat.err_code() == SQLITE_TOOBIG) {
        sqlite3_result_error_toobig(ctx);
      } else {
        sqlite3_result_error(ctx, stat.err_msg(), -1);
        sqlite3_result_error_code(ctx, stat.err_code());
      }
      return true;
    }
    return false;
  }

  /** @brief Factory constructor returning a successful void result. */
  static inline SqliteResult<void> ok() noexcept {
    return SqliteResult<void>();
  }

  /** @brief Factory constructor returning an error void result. */
  static inline SqliteResult<void> err(int code,
                                       const char *msg = nullptr) noexcept {
    return SqliteResult<void>(code, msg);
  }

  /** @brief Factory constructor returning an out-of-memory error void result.
   */
  static inline SqliteResult<void>
  nomem(const char *msg = "Out of memory") noexcept {
    return SqliteResult<void>(SQLITE_NOMEM, msg);
  }
};

/**
 * @brief Early-returns if the given SqliteResult or SqliteStatus is an error,
 * assigning the unwrapped payload into `target`. Portable across MSVC, GCC, and
 * Clang.
 */
#define SQLITE_TRY_ASSIGN(target, expr)                                        \
  do {                                                                         \
    auto _sqlite_try_res = (expr);                                             \
    if (_sqlite_try_res.is_err()) {                                            \
      return _sqlite_try_res.status();                                         \
    }                                                                          \
    target = _sqlite_try_res.take_value();                                     \
  } while (0)

#if defined(__GNUC__) || defined(__clang__)
/**
 * @brief GNU Statement expression macro early-returning on error and
 * evaluating to the unwrapped payload on success (similar to Rust `?`).
 */
#define SQLITE_TRY(expr)                                                       \
  ({                                                                           \
    auto _sqlite_try_res = (expr);                                             \
    if (_sqlite_try_res.is_err()) {                                            \
      return _sqlite_try_res.status();                                         \
    }                                                                          \
    _sqlite_try_res.take_value();                                              \
  })
#endif

/**
 * @brief Attempts to allocate and construct an object of type `T`, returning a
 * Rust-style SqliteResult.
 *
 * Allocates heap memory via `sqlite3_malloc` and in-place constructs the object
 * using forwarded arguments. Returns `SQLITE_NOMEM` with a descriptive message
 * if memory allocation fails.
 *
 * @tparam T Object type to allocate and construct.
 * @tparam Args Forwarded constructor argument types.
 * @param args Arguments forwarded to `T`'s constructor.
 * @return SqliteResult<T*> containing pointer on success, or SQLITE_NOMEM on
 * failure.
 */
template <typename T, typename... Args>
inline SqliteResult<T *> sqlite_try_new(Args &&...args) {
  T *ptr = sqlite_new<T>(sqlite_forward<Args>(args)...);
  if (!ptr) {
    return SqliteResult<T *>::nomem(
        "Memory allocation failed in sqlite_try_new");
  }
  return SqliteResult<T *>::ok(ptr);
}

/**
 * @brief Attempts to allocate an array of `count` elements of type `T`,
 * returning a Rust-style SqliteResult.
 *
 * Allocates raw uninitialized memory via `sqlite3_malloc64`. Includes size_t
 * overflow protection.
 *
 * @tparam T Element type.
 * @param count Number of elements to allocate space for.
 * @return SqliteResult<T*> containing array pointer on success, or SQLITE_NOMEM
 * / SQLITE_TOOBIG on failure.
 */
template <typename T>
inline SqliteResult<T *> sqlite_try_new_array(size_t count) {
  if (count == 0) {
    return SqliteResult<T *>::ok(nullptr);
  }
  if (count > static_cast<size_t>(-1) / sizeof(T)) {
    return SqliteResult<T *>::err(
        SQLITE_TOOBIG,
        "Array allocation size overflow in sqlite_try_new_array");
  }
  T *ptr = sqlite_new_array<T>(count);
  if (!ptr) {
    return SqliteResult<T *>::nomem(
        "Memory allocation failed in sqlite_try_new_array");
  }
  return SqliteResult<T *>::ok(ptr);
}

/**
 * @brief Attempts to allocate raw zero-initialized heap memory for an object of
 * type `T`, returning a Rust-style SqliteResult.
 *
 * Allocates raw zero-initialized bytes via `sqlite3_malloc` and `memset`
 * without invoking constructors.
 *
 * @tparam T Object type.
 * @return SqliteResult<T*> containing pointer to zeroed memory on success, or
 * SQLITE_NOMEM on failure.
 */
template <typename T> inline SqliteResult<T *> sqlite_try_new_zeroed() {
  T *ptr = sqlite_new_zeroed<T>();
  if (!ptr) {
    return SqliteResult<T *>::nomem(
        "Memory allocation failed in sqlite_try_new_zeroed");
  }
  return SqliteResult<T *>::ok(ptr);
}

/**
 * @brief Attempts to allocate an array of `count` zero-initialized elements of
 * type `T`, returning a Rust-style SqliteResult.
 *
 * Allocates raw zero-initialized memory via `sqlite3_malloc64` and `memset`.
 * Includes size_t overflow protection.
 *
 * @tparam T Element type.
 * @param count Number of elements to allocate space for.
 * @return SqliteResult<T*> containing array pointer on success, or SQLITE_NOMEM
 * / SQLITE_TOOBIG on failure.
 */
template <typename T>
inline SqliteResult<T *> sqlite_try_new_array_zeroed(size_t count) {
  if (count == 0) {
    return SqliteResult<T *>::ok(nullptr);
  }
  if (count > static_cast<size_t>(-1) / sizeof(T)) {
    return SqliteResult<T *>::err(
        SQLITE_TOOBIG,
        "Array allocation size overflow in sqlite_try_new_array_zeroed");
  }
  T *ptr = sqlite_new_array_zeroed<T>(count);
  if (!ptr) {
    return SqliteResult<T *>::nomem(
        "Memory allocation failed in sqlite_try_new_array_zeroed");
  }
  return SqliteResult<T *>::ok(ptr);
}

/**
 * @brief Attempts to reallocate an array of type `T`, zero-initializing newly
 * expanded elements, returning a Rust-style SqliteResult.
 *
 * @tparam T Element type.
 * @param ptr Existing array pointer (or nullptr).
 * @param old_count Previous element count.
 * @param new_count Target new element count.
 * @return SqliteResult<T*> containing reallocated array on success, or
 * SQLITE_NOMEM / SQLITE_TOOBIG on failure.
 */
template <typename T>
inline SqliteResult<T *>
sqlite_try_reallocate_array_zeroed(T *ptr, size_t old_count, size_t new_count) {
  if (new_count == 0) {
    if (ptr)
      sqlite_delete_array(ptr);
    return SqliteResult<T *>::ok(nullptr);
  }
  if (new_count > static_cast<size_t>(-1) / sizeof(T)) {
    return SqliteResult<T *>::err(SQLITE_TOOBIG,
                                  "Array reallocation size overflow in "
                                  "sqlite_try_reallocate_array_zeroed");
  }
  T *new_ptr = sqlite_reallocate_array_zeroed<T>(ptr, old_count, new_count);
  if (!new_ptr) {
    return SqliteResult<T *>::nomem(
        "Memory reallocation failed in sqlite_try_reallocate_array_zeroed");
  }
  return SqliteResult<T *>::ok(new_ptr);
}

// ============================================================================
// FREESTANDING MEMORY COMPARISON & STRING UTILITIES
// ============================================================================

namespace SqliteMemoryUtil {
    /**
     * @brief Performs a fast lexicographical memory comparison.
     */
    inline bool memcmp_less(const void* val1, int len1, const void* val2, int len2) {
        int min_len = (len1 < len2) ? len1 : len2;
        int cmp = memcmp(val1, val2, min_len);
        if (cmp != 0) {
            return cmp < 0;
        }
        return len1 < len2;
    }

    /**
     * @brief Performs a fast lexicographical equality check.
     */
    inline bool memcmp_equal(const void* val1, int len1, const void* val2, int len2) {
        if (len1 != len2) return false;
        if (len1 == 0) return true;
        if (val1 == val2) return true;
        if (!val1 || !val2) return false;
        return memcmp(val1, val2, len1) == 0;
    }
}

namespace SqliteStringUtil {
    /**
     * @brief Computes the length of a null-terminated C-string with nullptr safety.
     */
    inline int sqlite_strlen(const char* str) {
        return str ? static_cast<int>(strlen(str)) : 0;
    }
}

#endif // SQLITE3_ALLOCATOR_HPP
