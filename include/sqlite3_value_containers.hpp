/**
 * @file sqlite3_value_containers.hpp
 * @brief Unified C++11 Value Containers & Scope Dispatcher (-nostdlib++
 * compliant).
 *
 * Provides a footprint-optimized, zero-dependency pair of value container
 * templates:
 *
 * 1. `SqliteValueTuple<size_t N = 0>`:
 *    - Specialization $N \in [1..8]$: Exact $N \times 16\text{B}$ in-situ stack
 * array. Zero heap allocations, zero capacity overhead, exact L1 cache line
 * alignment (16B, 32B, 48B, 64B, 128B). Designed for compile-time fixed-arity
 * Primary Keys, Composite Index Keys, and Fixed Records.
 *    - Specialization $N = 0$ (default `SqliteValueTuple<>`): Direct dynamic heap tuple
 * with runtime-sized buffer via `sqlite3_malloc64`.
 *
 * 2. `SqliteValueVec<size_t N = 0>`:
 *    - Specialization $N \in [1..8]$: Small Buffer Optimized (SBO) dynamic
 * vector with $N \times 16\text{B}$ in-situ stack storage. Seamlessly spills to
 * heap (`sqlite3_malloc64`) when resized $> N$, and safely returns to stack
 * when shrunk back $\le N$.
 *    - Specialization $N = 0$ (default `SqliteValueVec<>`): Direct dynamic heap vector with 0 stack SBO overhead.
 *
 * 3. `withSqliteRowOwned(int size, Callable&& fn)`:
 *    - Zero-heap stack allocation dispatcher evaluating runtime column counts
 * (1..8) to allocate exact `SqliteValueTuple<1..8>` on the stack with 0 heap
 * allocations, passing an ergonomic `SqliteRowOwnedWrapper` span to the user
 * callback.
 *
 * All containers synthesize:
 *  - `SQLITE_DERIVE_ARRAY_ACCESSORS` (typed getters `as_int64()`, `as_text()`,
 * `as_blob()`, etc.)
 *  - `SQLITE_DERIVE_ARRAY_ITERATOR` (C++11 range-based `for` loop iterators)
 *  - Full relational operators (`==`, `!=`, `<`, `<=`, `>`, `>=`)
 *  - Heterogeneous Swiss Table (`SqliteRowHash`, `SqliteRowEqual`) and B-Tree
 * (`SqliteRowLess`) support.
 */

#ifndef SQLITE3_VALUE_CONTAINERS_HPP
#define SQLITE3_VALUE_CONTAINERS_HPP

#include "sqlite3_allocator.hpp"
#include "sqlite3_row.hpp"
#include "sqlite3_value.hpp"
#include <sqlite3.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if (defined(__cplusplus) && __cplusplus >= 201103L) ||                        \
    (defined(_MSVC_LANG) && _MSVC_LANG >= 201103L)
#include <initializer_list>
#define SQLITE_CONTAINERS_HAS_INITIALIZER_LIST 1
#endif

// ============================================================================
// 0. FREESTANDING SFINAE UTILITIES & INTERNAL TYPE TRAITS (-nostdlib++
// compliant)
// ============================================================================

/**
 * @brief Freestanding SFINAE conditional type enabler (`-nostdlib++`
 * replacement for `std::enable_if`).
 * @tparam B Boolean condition.
 * @tparam T Type to define when condition B is true.
 */
template <bool B, typename T = void> struct sqlite_container_enable_if {};
template <typename T> struct sqlite_container_enable_if<true, T> {
  typedef T type;
};

// Forward declarations for container template predicates
template <size_t N = 0, typename Enable = void> class SqliteValueTuple;
template <size_t N = 0, typename Enable = void> class SqliteValueVec;

namespace sqlite_container_internal {
// ------------------------------------------------------------------------
// Freestanding Type Transformation Traits (std::decay replacement)
// ------------------------------------------------------------------------

template <typename T> struct remove_cv {
  typedef T type;
};
template <typename T> struct remove_cv<const T> {
  typedef T type;
};
template <typename T> struct remove_cv<volatile T> {
  typedef T type;
};
template <typename T> struct remove_cv<const volatile T> {
  typedef T type;
};

template <typename T> struct remove_ref {
  typedef T type;
};
template <typename T> struct remove_ref<T &> {
  typedef T type;
};
template <typename T> struct remove_ref<T &&> {
  typedef T type;
};

/**
 * @brief Freestanding decay implementation stripping const/volatile qualifiers
 * and references.
 */
template <typename T> struct decay {
  typedef typename remove_cv<typename remove_ref<T>::type>::type type;
};

// ------------------------------------------------------------------------
// Row Container Discrimination Traits
// ------------------------------------------------------------------------

/**
 * @brief Identifies multi-column row container types to prevent ambiguous
 * overload resolution.
 */
template <typename T> struct is_row_container {
  static const bool value = false;
};
template <size_t N, typename E>
struct is_row_container<SqliteValueTuple<N, E>> {
  static const bool value = true;
};
template <size_t N, typename E> struct is_row_container<SqliteValueVec<N, E>> {
  static const bool value = true;
};
template <> struct is_row_container<SqliteRowOwnedWrapper> {
  static const bool value = true;
};
template <> struct is_row_container<SqliteRowView> {
  static const bool value = true;
};

// ------------------------------------------------------------------------
// Pointer and Scalar Type Traits
// ------------------------------------------------------------------------

template <typename T> struct is_pointer {
  static const bool value = false;
};
template <typename T> struct is_pointer<T *> {
  static const bool value = true;
};

template <typename T, typename U> struct is_same {
  static const bool value = false;
};
template <typename T> struct is_same<T, T> {
  static const bool value = true;
};

/**
 * @brief Checks if T is a pointer to a non-character type (e.g. int*, double*).
 * Used to differentiate array buffer pointers `(ptr, count)` from string
 * literals / text values.
 */
template <typename T> struct is_non_char_pointer {
  static const bool value = is_pointer<T>::value &&
                            !is_same<T, const char *>::value &&
                            !is_same<T, char *>::value;
};

/**
 * @brief Identifies character types to prevent string literals (e.g. const
 * char[16]) from being erroneously matched as fixed-size C-arrays of individual
 * char values.
 */
template <typename T> struct is_char_type {
  static const bool value = false;
};
template <> struct is_char_type<char> {
  static const bool value = true;
};
template <> struct is_char_type<signed char> {
  static const bool value = true;
};
template <> struct is_char_type<unsigned char> {
  static const bool value = true;
};
template <> struct is_char_type<wchar_t> {
  static const bool value = true;
};
template <> struct is_char_type<char16_t> {
  static const bool value = true;
};
template <> struct is_char_type<char32_t> {
  static const bool value = true;
};

/**
 * @brief Primary SFINAE selector determining if T0 is a valid first argument
 * for variadic constructors. Disables the variadic template when T0 is a row
 * container (to prioritize projecting constructors) or a non-char pointer (to
 * prioritize pointer + count slicing constructors).
 */
template <typename T> struct is_valid_variadic_arg {
  static const bool value =
      !is_row_container<T>::value && !is_non_char_pointer<T>::value;
};

// ------------------------------------------------------------------------
// Hash Combiner Engine
// ------------------------------------------------------------------------

/**
 * @brief Compile-time recursive / iterative tuple hash combiner.
 * @tparam N Fixed column count in tuple.
 */
template <size_t N> struct TupleHashHelper {
  /**
   * @brief Computes composite 64-bit MurmurHash2 across N elements.
   * @param vals Pointer to contiguous array of SqliteValueOwned elements.
   * @return 64-bit composite hash value.
   */
  static inline unsigned long long
  compute(const SqliteValueOwned *vals) noexcept {
    uint64_t h = SqliteHashUtil::DEFAULT_SEED;
    for (size_t i = 0; i < N; ++i) {
      h = SqliteHashUtil::combine(h, vals[i].hash());
    }
    return h;
  }
};

/**
 * @brief Specialized scalar fast-path hash combiner for 1-column tuples (N =
 * 1).
 */
template <> struct TupleHashHelper<1> {
  /**
   * @brief Directly forwards hash of single element with zero loop or
   * combination overhead.
   * @param vals Pointer to single SqliteValueOwned element.
   * @return 64-bit hash value of vals[0].
   */
  static inline unsigned long long
  compute(const SqliteValueOwned *vals) noexcept {
    return vals[0].hash();
  }
};
} // namespace sqlite_container_internal

// ============================================================================
// STANDARD TUPLE & VECTOR MODIFIER SYNTHESIS MACROS
// ============================================================================

#ifndef SQLITE_DERIVE_STD_TUPLE_MODIFIERS
/**
 * @def SQLITE_DERIVE_STD_TUPLE_MODIFIERS
 * @brief Synthesizes fill() modifiers for fixed-size tuple containers.
 * @param DataPtr Contiguous pointer to beginning of elements.
 * @param SizeVal Number of elements to fill.
 */
#define SQLITE_DERIVE_STD_TUPLE_MODIFIERS(DataPtr, SizeVal) \
  /** @brief Overwrites all tuple elements with clones of the given owned value. */ \
  inline void fill(const SqliteValueOwned& val) { \
    size_type sz = static_cast<size_type>(SizeVal); \
    for (size_type i = 0; i < sz; ++i) (DataPtr)[i] = val.clone(); \
  } \
  /** @brief Overwrites all tuple elements with values constructed from a primitive literal. */ \
  template <typename TPrimitive, typename sqlite_enable_if<!sqlite_is_same<typename sqlite_remove_reference<TPrimitive>::type, SqliteValueOwned>::value, int>::type = 0> \
  inline void fill(const TPrimitive& val) { \
    size_type sz = static_cast<size_type>(SizeVal); \
    for (size_type i = 0; i < sz; ++i) (DataPtr)[i] = SqliteValueOwned(val); \
  }
#endif

#ifndef SQLITE_DERIVE_STD_VEC_METHODS
/**
 * @def SQLITE_DERIVE_STD_VEC_METHODS
 * @brief Synthesizes complete std::vector compliant modifiers (max_size, resize with value, insert, erase, assign, swap).
 * @param ContainerType The concrete vector type name.
 */
#define SQLITE_DERIVE_STD_VEC_METHODS(ContainerType) \
  /** @brief Returns maximum theoretical element capacity for the vector. */ \
  inline constexpr size_type max_size() const noexcept { return static_cast<size_type>(-1) / sizeof(SqliteValueOwned); } \
  /** @brief Resizes container, appending cloned copies of val if count > size(). */ \
  inline void resize(size_type count, const SqliteValueOwned& val) { \
    size_type old_sz = static_cast<size_type>(size()); \
    resize(static_cast<int>(count)); \
    for (size_type i = old_sz; i < count; ++i) { \
      (*this)[static_cast<int>(i)] = val.clone(); \
    } \
  } \
  /** @brief Resizes container, appending primitive-constructed values if count > size(). */ \
  template <typename TPrimitive, typename sqlite_enable_if<!sqlite_is_same<typename sqlite_remove_reference<TPrimitive>::type, SqliteValueOwned>::value, int>::type = 0> \
  inline void resize(size_type count, const TPrimitive& val) { \
    size_type old_sz = static_cast<size_type>(size()); \
    resize(static_cast<int>(count)); \
    for (size_type i = old_sz; i < count; ++i) { \
      (*this)[static_cast<int>(i)] = SqliteValueOwned(val); \
    } \
  } \
  /** @brief Inserts a cloned copy of an owned value before pos. */ \
  inline iterator insert(const_iterator pos, const SqliteValueOwned& val) { \
    difference_type idx = pos - cbegin(); \
    if (idx < 0) idx = 0; \
    if (idx > static_cast<difference_type>(size())) idx = static_cast<difference_type>(size()); \
    int sz = size(); \
    resize(sz + 1); \
    pointer d = data(); \
    for (int i = sz; i > idx; --i) { \
      d[i] = sqlite_move(d[i - 1]); \
    } \
    d[idx] = val.clone(); \
    return d + idx; \
  } \
  /** @brief Moves an owned rvalue into position before pos. */ \
  inline iterator insert(const_iterator pos, SqliteValueOwned&& val) { \
    difference_type idx = pos - cbegin(); \
    if (idx < 0) idx = 0; \
    if (idx > static_cast<difference_type>(size())) idx = static_cast<difference_type>(size()); \
    int sz = size(); \
    resize(sz + 1); \
    pointer d = data(); \
    for (int i = sz; i > idx; --i) { \
      d[i] = sqlite_move(d[i - 1]); \
    } \
    d[idx] = sqlite_move(val); \
    return d + idx; \
  } \
  /** @brief Inserts a value constructed from a primitive literal before pos. */ \
  template <typename TPrimitive, typename sqlite_enable_if<!sqlite_is_same<typename sqlite_remove_reference<TPrimitive>::type, SqliteValueOwned>::value, int>::type = 0> \
  inline iterator insert(const_iterator pos, const TPrimitive& val) { \
    return insert(pos, SqliteValueOwned(val)); \
  } \
  /** @brief Inserts count copies of an owned value before pos. */ \
  inline iterator insert(const_iterator pos, size_type count, const SqliteValueOwned& val) { \
    difference_type idx = pos - cbegin(); \
    if (idx < 0) idx = 0; \
    if (idx > static_cast<difference_type>(size())) idx = static_cast<difference_type>(size()); \
    int sz = size(); \
    resize(sz + static_cast<int>(count)); \
    pointer d = data(); \
    for (int i = sz - 1; i >= idx; --i) { \
      d[i + count] = sqlite_move(d[i]); \
    } \
    for (size_type i = 0; i < count; ++i) { \
      d[idx + i] = val.clone(); \
    } \
    return d + idx; \
  } \
  /** @brief Inserts count copies constructed from a primitive literal before pos. */ \
  template <typename TPrimitive, typename sqlite_enable_if<!sqlite_is_same<typename sqlite_remove_reference<TPrimitive>::type, SqliteValueOwned>::value, int>::type = 0> \
  inline iterator insert(const_iterator pos, size_type count, const TPrimitive& val) { \
    return insert(pos, count, SqliteValueOwned(val)); \
  } \
  /** @brief Erases element at pos. */ \
  inline iterator erase(const_iterator pos) { \
    difference_type idx = pos - cbegin(); \
    int sz = size(); \
    if (idx >= 0 && idx < sz) { \
      pointer d = data(); \
      for (int i = static_cast<int>(idx); i + 1 < sz; ++i) { \
        d[i] = sqlite_move(d[i + 1]); \
      } \
      resize(sz - 1); \
      return data() + idx; \
    } \
    return end(); \
  } \
  /** @brief Erases elements in the range [first, last). */ \
  inline iterator erase(const_iterator first, const_iterator last) { \
    difference_type idx_first = first - cbegin(); \
    difference_type idx_last = last - cbegin(); \
    int sz = size(); \
    if (idx_first < 0) idx_first = 0; \
    if (idx_last > sz) idx_last = sz; \
    if (idx_first < idx_last) { \
      difference_type cnt = idx_last - idx_first; \
      pointer d = data(); \
      for (int i = static_cast<int>(idx_first); i + cnt < sz; ++i) { \
        d[i] = sqlite_move(d[i + cnt]); \
      } \
      resize(sz - static_cast<int>(cnt)); \
      return data() + idx_first; \
    } \
    return data() + idx_first; \
  } \
  /** @brief Replaces contents with count copies of the given owned value. */ \
  inline void assign(size_type count, const SqliteValueOwned& val) { \
    clear(); \
    resize(static_cast<int>(count)); \
    for (size_type i = 0; i < count; ++i) { \
      (*this)[static_cast<int>(i)] = val.clone(); \
    } \
  } \
  /** @brief Replaces contents with count copies constructed from a primitive literal. */ \
  template <typename TPrimitive, typename sqlite_enable_if<!sqlite_is_same<typename sqlite_remove_reference<TPrimitive>::type, SqliteValueOwned>::value, int>::type = 0> \
  inline void assign(size_type count, const TPrimitive& val) { \
    clear(); \
    resize(static_cast<int>(count)); \
    for (size_type i = 0; i < count; ++i) { \
      (*this)[static_cast<int>(i)] = SqliteValueOwned(val); \
    } \
  } \
  /** @brief Replaces contents with elements from the given pointer/iterator range [first, last). */ \
  template <typename InputIt> \
  inline void assign(InputIt* first, InputIt* last) { \
    clear(); \
    for (; first != last; ++first) { \
      push_back(*first); \
    } \
  } \
  /** @brief Swaps the contents of this vector with other via zero-allocation move semantics. */ \
  inline void swap(ContainerType& other) noexcept { \
    ContainerType tmp = sqlite_move(*this); \
    *this = sqlite_move(other); \
    other = sqlite_move(tmp); \
  }
#endif

// ============================================================================
// GENERIC CONSTRUCTOR SYNTHESIS MACROS
// ============================================================================

/**
 * @def SQLITE_DERIVE_PRIMITIVE_CONSTRUCTORS(ContainerType)
 * @brief Synthesizes scalar initializing constructors for non-integer primitive
 * types. Places the converted scalar value in element 0 and pre-initializes
 * remaining slots to SQLITE_NULL.
 */
#define SQLITE_DERIVE_PRIMITIVE_CONSTRUCTORS(ContainerType)                    \
  /** @brief Single-element initializing constructor from double. */           \
  inline explicit ContainerType(double val) { init_from_range(&val, 1); }      \
  /** @brief Single-element initializing constructor from float. */            \
  inline explicit ContainerType(float val) {                                   \
    double d = val;                                                            \
    init_from_range(&d, 1);                                                    \
  }                                                                            \
  /** @brief Single-element initializing constructor from null-terminated      \
   * C-string. */                                                              \
  inline explicit ContainerType(const char *val) { init_from_range(&val, 1); } \
  /** @brief Single-element initializing constructor from SqliteStringView. */ \
  inline explicit ContainerType(const SqliteStringView &val) {                 \
    init_from_range(&val, 1);                                                  \
  }                                                                            \
  /** @brief Single-element initializing constructor from SqliteBlobView. */   \
  inline explicit ContainerType(const SqliteBlobView &val) {                   \
    init_from_range(&val, 1);                                                  \
  }                                                                            \
  /** @brief Single-element initializing constructor from SqliteValueOwned. */ \
  inline explicit ContainerType(const SqliteValueOwned &val) {                 \
    init_from_range(&val, 1);                                                  \
  }                                                                            \
  /** @brief Single-element initializing constructor from SqliteValueView. */  \
  inline explicit ContainerType(const SqliteValueView &val) {                  \
    init_from_range(&val, 1);                                                  \
  }

#if defined(SQLITE_CONTAINERS_HAS_INITIALIZER_LIST)
/**
 * @def SQLITE_DERIVE_GENERIC_INITIALIZER_LIST_CONSTRUCTOR(ContainerType)
 * @brief Synthesizes homogeneous C++11 initializer list constructor (`t = { 10,
 * 20, 30 };`).
 */
#define SQLITE_DERIVE_GENERIC_INITIALIZER_LIST_CONSTRUCTOR(ContainerType)      \
  /** @brief Generic initializer list constructor accepting any convertible    \
   * type (int, double, const char*, SqliteValueOwned, SqliteValueView, etc.). \
   */                                                                          \
  template <typename TValueType>                                               \
  inline ContainerType(std::initializer_list<TValueType> list) {               \
    init_from_range(list.begin(), static_cast<int>(list.size()));              \
  }
#else
#define SQLITE_DERIVE_GENERIC_INITIALIZER_LIST_CONSTRUCTOR(ContainerType)
#endif

/**
 * @def SQLITE_DERIVE_GENERIC_ARRAY_CONSTRUCTORS(ContainerType)
 * @brief Synthesizes array and variadic constructor overloads:
 *  1. Pointer + count buffer slicing: `Container(ptr, count)` (both const and
 * non-const).
 *  2. Fixed-size C-array references: `Container(arr)` (auto-infers compile-time
 * size M).
 *  3. Heterogeneous variadic pack: `Container(v0, v1, rest...)` supporting
 * mixed types with zero heap allocation.
 *  4. Initializer list constructor: `Container({ ... })`.
 */
#define SQLITE_DERIVE_GENERIC_ARRAY_CONSTRUCTORS(ContainerType)                \
  /** @brief Generic contiguous array constructor from non-const pointer +     \
   * count of any convertible type. */                                         \
  template <typename TValueType, typename sqlite_container_enable_if<          \
                                     !sqlite_container_internal::is_char_type< \
                                         typename sqlite_container_internal::  \
                                             decay<TValueType>::type>::value,  \
                                     int>::type = 0>                           \
  inline ContainerType(TValueType *arr, int count) {                           \
    init_from_range(arr, count);                                               \
  }                                                                            \
  /** @brief Generic contiguous array constructor from const pointer + count   \
   * of any convertible type. */                                               \
  template <typename TValueType, typename sqlite_container_enable_if<          \
                                     !sqlite_container_internal::is_char_type< \
                                         typename sqlite_container_internal::  \
                                             decay<TValueType>::type>::value,  \
                                     int>::type = 0>                           \
  inline ContainerType(const TValueType *arr, int count) {                     \
    init_from_range(arr, count);                                               \
  }                                                                            \
  /** @brief Generic fixed-size C-array reference constructor of any           \
   * convertible type. */                                                      \
  template <typename TValueType, size_t M,                                     \
            typename sqlite_container_enable_if<                               \
                !sqlite_container_internal::is_char_type<                      \
                    typename sqlite_container_internal::decay<                 \
                        TValueType>::type>::value,                             \
                int>::type = 0>                                                \
  inline explicit ContainerType(const TValueType(&arr)[M]) {                   \
    init_from_range(arr, static_cast<int>(M));                                 \
  }                                                                            \
  /** @brief Variadic constructor accepting 2 or more heterogeneous arguments  \
   * of arbitrary types (int, text, double, etc.). */                          \
  template <                                                                   \
      typename T0, typename T1, typename... TRest,                             \
      typename sqlite_container_enable_if<                                     \
          sqlite_container_internal::is_valid_variadic_arg<                    \
              typename sqlite_container_internal::decay<T0>::type>::value,     \
          int>::type = 0>                                                      \
  inline explicit ContainerType(T0 &&v0, T1 &&v1, TRest &&...rest) {           \
    const SqliteValueOwned tmp[] = {                                           \
        SqliteValueOwned(static_cast<T0 &&>(v0)),                              \
        SqliteValueOwned(static_cast<T1 &&>(v1)),                              \
        SqliteValueOwned(static_cast<TRest &&>(rest))...};                     \
    init_from_range(tmp, static_cast<int>(sizeof...(TRest) + 2));              \
  }                                                                            \
  SQLITE_DERIVE_GENERIC_INITIALIZER_LIST_CONSTRUCTOR(ContainerType)

/**
 * @def SQLITE_DERIVE_TUPLE_CONSTRUCTORS(ContainerType)
 * @brief Synthesizes the complete constructor suite for SqliteValueTuple.
 * Includes single-element integer constructors (int, int64_t) in addition to
 * all generic and primitive constructors.
 */
#define SQLITE_DERIVE_TUPLE_CONSTRUCTORS(ContainerType)                        \
  /** @brief Single-element initializing constructor from int. */              \
  inline explicit ContainerType(int val) { init_from_range(&val, 1); }         \
  /** @brief Single-element initializing constructor from 64-bit integer. */   \
  inline explicit ContainerType(int64_t val) { init_from_range(&val, 1); }     \
  SQLITE_DERIVE_PRIMITIVE_CONSTRUCTORS(ContainerType)                          \
  SQLITE_DERIVE_GENERIC_ARRAY_CONSTRUCTORS(ContainerType)

/**
 * @def SQLITE_DERIVE_VEC_CONSTRUCTORS(ContainerType)
 * @brief Synthesizes the complete constructor suite for SqliteValueVec.
 * Excludes single-element int constructor to avoid conflict with the sized
 * capacity constructor `Container(int count)`.
 */
#define SQLITE_DERIVE_VEC_CONSTRUCTORS(ContainerType)                          \
  SQLITE_DERIVE_PRIMITIVE_CONSTRUCTORS(ContainerType)                          \
  SQLITE_DERIVE_GENERIC_ARRAY_CONSTRUCTORS(ContainerType)

// ============================================================================
// PART 1: SqliteValueTuple<size_t N = 1> (Fixed-Arity Compile-Time Tuple)
// ============================================================================

/**
 * @brief Primary template declaration for SqliteValueTuple.
 * @tparam N Fixed column count.
 * @tparam Enable SFINAE specialization selector.
 */
template <size_t N, typename Enable> class SqliteValueTuple;

/**
 * @brief Specialization 1: N in [1..8] (Exact In-Situ Static Memory, 0 Heap
 * Allocations).
 *
 * Stores exactly N elements in an in-situ stack array of `SqliteValueOwned[N]`.
 * Memory footprint is exactly `N * 16` bytes.
 *
 * @tparam N Fixed column count between 1 and 8 inclusive.
 */
template <size_t N>
class SqliteValueTuple<
    N, typename sqlite_container_enable_if<(N >= 1 && N <= 8)>::type> {
  static_assert(N >= 1 && N <= 8,
                "SqliteValueTuple fixed specialization is for N = 1..8");

protected:
  SqliteValueOwned m_values[N]; ///< Fixed in-situ stack storage (N * 16 bytes).

private:
  /**
   * @brief Pre-initializes all N in-situ slots with canonical SQLITE_NULL
   * values via single-burst SIMD memcpy.
   *
   * ### Single-Burst SIMD Initialization (`init_null_values`):
   * Instead of executing scalar constructor loops or branchy element-by-element
   * initialization, this helper copies `N * 16` bytes directly from
   * `SqliteValueOwned::static_null_array()`.
   *
   * - `sizeof(SqliteValueTuple<N>) == N * 16` is a compile-time constant.
   * - GCC/Clang/MSVC lower this `memcpy` to 1–4 vector register operations
   * (`movups` / `vmovups`), initializing the entire stack array in 1–2 CPU
   * clock cycles (~0.3–0.6 ns).
   * - Guarantees all N slots start with `tag.raw == 0xA0 >= 0x20` (active
   * SQLITE_NULL), ready for immediate use.
   */
  inline void init_null_values() noexcept {
    memcpy(static_cast<void *>(m_values),
           static_cast<const void *>(SqliteValueOwned::static_null_array()),
           N * sizeof(SqliteValueOwned));
  }

  /**
   * @brief Range initializer populating in-situ slots from contiguous source
   * array.
   *
   * ### Initialization Semantics:
   * - First pre-initializes all N slots to canonical `SQLITE_NULL` via
   * single-burst SIMD memcpy.
   * - Populates up to `min(N, count)` elements by converting each source
   * element to `SqliteValueOwned`.
   * - Trailing slots beyond `count` remain guaranteed canonical `SQLITE_NULL`.
   *
   * @tparam TValueType Source type convertible to SqliteValueOwned (int,
   * double, string, view, etc.).
   * @param src Pointer to contiguous array of source elements.
   * @param count Number of elements in src.
   */
  template <typename TValueType>
  inline void init_from_range(const TValueType *src, int count) noexcept {
    init_null_values();
    if (src && count > 0) {
      int limit = static_cast<int>(N) < count ? static_cast<int>(N) : count;
      for (int i = 0; i < limit; ++i) {
        m_values[i] = SqliteValueOwned(src[i]);
      }
    }
  }

public:
  /**
   * @brief Default constructor. Constructs N empty SQLITE_NULL values via
   * single-burst SIMD memcpy.
   */
  inline SqliteValueTuple() noexcept { init_null_values(); }

  // Synthesizes all primitive, array, and variadic heterogeneous constructors
  SQLITE_DERIVE_TUPLE_CONSTRUCTORS(SqliteValueTuple)

  /**
   * @brief Multi-column initializing constructor from SqliteRowView.
   * @param view_arr Source multi-column row view. Copies min(N,
   * view_arr.size()) columns.
   */
  inline explicit SqliteValueTuple(const SqliteRowView &view_arr) noexcept {
    init_null_values();
    int limit = static_cast<int>(N) < view_arr.size() ? static_cast<int>(N)
                                                      : view_arr.size();
    for (int i = 0; i < limit; ++i) {
      m_values[i] = view_arr[i].to_owned();
    }
  }

  /**
   * @brief Projecting constructor extracting indexed columns from a full row
   * container.
   * @tparam RowType Source container supporting `operator[](int)` and
   * `.size()`.
   * @param full_row Complete row containing all table columns.
   * @param col_indices Array of column indices to project (e.g. primary key
   * indices). If null, uses identity 0..N-1.
   * @param count Number of indices in col_indices.
   */
  template <typename RowType>
  inline SqliteValueTuple(const RowType &full_row, const int *col_indices,
                          int count) noexcept {
    init_null_values();
    int limit = static_cast<int>(N) < count ? static_cast<int>(N) : count;
    for (int i = 0; i < limit; ++i) {
      int col = col_indices ? col_indices[i] : i;
      if (col >= 0 && col < full_row.size()) {
        m_values[i] = full_row[col].clone();
      }
    }
  }

  /**
   * @brief Deep copy constructor. Clones all N elements.
   * @param other Source tuple.
   */
  inline SqliteValueTuple(const SqliteValueTuple &other) {
    for (size_t i = 0; i < N; ++i)
      m_values[i] = other.m_values[i].clone();
  }

  /**
   * @brief Deep copy assignment operator.
   * @param other Source tuple.
   * @return Reference to this tuple.
   */
  inline SqliteValueTuple &operator=(const SqliteValueTuple &other) {
    if (this != &other) {
      for (size_t i = 0; i < N; ++i)
        m_values[i] = other.m_values[i].clone();
    }
    return *this;
  }

  /**
   * @brief Default move constructor. Transfers inline ownership.
   */
  inline SqliteValueTuple(SqliteValueTuple &&other) noexcept = default;

  /**
   * @brief Default move assignment operator.
   */
  inline SqliteValueTuple &
  operator=(SqliteValueTuple &&other) noexcept = default;

  // Standard C++ Container Type Definitions
  SQLITE_DERIVE_STANDARD_CONTAINER_TYPEDEFS(SqliteValueOwned, SqliteValueOwned&, const SqliteValueOwned&, SqliteValueOwned*, const SqliteValueOwned*, SqliteValueOwned*, const SqliteValueOwned*)

  /** @brief Compile-time constant column count (0 runtime cycles). */
  inline constexpr int size() const noexcept { return static_cast<int>(N); }

  /** @brief Alias for size() returning the fixed column count. */
  inline constexpr int count() const noexcept { return static_cast<int>(N); }

  /** @brief Alias for size() returning the column count. */
  inline constexpr int column_count() const noexcept { return static_cast<int>(N); }

  /** @brief Checks if the tuple contains 0 columns. */
  inline constexpr bool empty() const noexcept { return N == 0; }

  /** @brief Always true for N in [1..8] (resides entirely on stack). */
  inline constexpr bool is_inline() const noexcept { return true; }

  /** @brief Always false for N in [1..8] (0 heap allocations). */
  inline constexpr bool is_heap() const noexcept { return false; }

  /** @brief Returns pointer to contiguous in-situ element array. */
  inline pointer       data() noexcept { return m_values; }

  /** @brief Returns const pointer to contiguous in-situ element array. */
  inline const_pointer data() const noexcept { return m_values; }

  /** @brief Resets all N columns in the tuple to SQLITE_NULL. */
  inline void set_null_all() noexcept {
    for (size_t i = 0; i < N; ++i) {
      m_values[i].set_null();
    }
  }

  // Standard Array Accessors, Iterators, and Modifiers
  SQLITE_DERIVE_STD_ARRAY_METHODS(m_values, N, fallback_null(), N)
  SQLITE_DERIVE_STD_TUPLE_MODIFIERS(m_values, N)

  /** @brief Swaps the contents of this tuple with another. */
  inline void swap(SqliteValueTuple& other) noexcept {
    for (size_t i = 0; i < N; ++i) {
      SqliteValueOwned tmp = sqlite_move(m_values[i]);
      m_values[i] = sqlite_move(other.m_values[i]);
      other.m_values[i] = sqlite_move(tmp);
    }
  }

private:
  static inline SqliteValueOwned& fallback_null() noexcept {
    return const_cast<SqliteValueOwned&>(SqliteValueOwned::static_null());
  }

public:
  /**
   * @brief Creates a lightweight non-owning span wrapper over this tuple.
   * @return 16-byte SqliteRowOwnedWrapper spanning m_values[0..N-1].
   */
  inline SqliteRowOwnedWrapper view() const noexcept {
    return SqliteRowOwnedWrapper(const_cast<SqliteValueOwned *>(m_values),
                                 static_cast<int>(N));
  }

  /**
   * @brief Implicit conversion operator to non-owning row span wrapper.
   */
  inline operator SqliteRowOwnedWrapper() const noexcept { return view(); }

  /**
   * @brief Computes 64-bit composite hash code for hash table lookups (Swiss
   * tables).
   * @return 64-bit MurmurHash2 hash value.
   */
  inline unsigned long long hash() const noexcept {
    return sqlite_container_internal::TupleHashHelper<N>::compute(m_values);
  }

  // Synthesized Typed Accessors & Full Relational Ops
  SQLITE_DERIVE_ARRAY_ACCESSORS
  SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteValueTuple)
  SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteRowOwnedWrapper)
  SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteRowView)
  SQLITE_DERIVE_ALL_SCALAR_RELATIONAL_OPS
};

/**
 * @brief Specialization 2: N == 0 (Direct Dynamic Heap Tuple).
 *
 * For tuple base capacity N == 0 (default dynamic heap tuple),
 * memory is allocated dynamically on the heap based on the size passed to the constructor.
 *
 * @tparam N Base capacity 0.
 */
template <size_t N>
class SqliteValueTuple<
    N, typename sqlite_container_enable_if<(N == 0)>::type> {
private:
  SqliteValueOwned *m_data =
      nullptr; ///< Contiguous heap buffer allocated via sqlite3_malloc64.
  uint32_t m_size = 0;     ///< Active element count.
  uint32_t m_capacity = 0; ///< Allocated element capacity.

  inline void release_memory() noexcept {
    if (m_data) {
      sqlite_destroy_n(m_data, m_size);
      sqlite_delete_array(m_data);
      m_data = nullptr;
    }
    m_size = 0;
    m_capacity = 0;
  }

  inline void allocate_memory(uint32_t cap) {
    release_memory();
    if (cap > 0) {
      m_data = sqlite_new_array<SqliteValueOwned>(cap);
      m_capacity = m_data ? cap : 0;
    }
  }

private:
  /**
   * @brief Pre-initializes heap slots with canonical SQLITE_NULL values via
   * SIMD memcpy chunks.
   *
   * Uses 8-element (128-byte) SIMD bursts from
   * SqliteValueOwned::static_null_array() followed by a single trailing
   * remainder memcpy, avoiding slow scalar constructor loops.
   */
  inline void init_null_values() noexcept {
    if (!m_data || m_size == 0)
      return;
    uint32_t i = 0;
    while (i + 8 <= m_size) {
      memcpy(static_cast<void *>(m_data + i),
             static_cast<const void *>(SqliteValueOwned::static_null_array()),
             8 * sizeof(SqliteValueOwned));
      i += 8;
    }
    if (i < m_size) {
      memcpy(static_cast<void *>(m_data + i),
             static_cast<const void *>(SqliteValueOwned::static_null_array()),
             (m_size - i) * sizeof(SqliteValueOwned));
    }
  }

  /**
   * @brief Range initializer allocating heap memory and populating elements
   * from source array.
   *
   * @tparam TValueType Source type convertible to SqliteValueOwned.
   * @param src Pointer to contiguous array of source elements.
   * @param count Number of elements in src.
   */
  template <typename TValueType>
  inline void init_from_range(const TValueType *src, int count) {
    uint32_t req_size = count > 0 ? static_cast<uint32_t>(count) : 0;
    allocate_memory(req_size);
    m_size = req_size;
    init_null_values();
    if (src && count > 0) {
      for (int i = 0; i < count; ++i) {
        m_data[i] = SqliteValueOwned(src[i]);
      }
    }
  }

public:
  /**
   * @brief Default constructor. Constructs an empty tuple (size = 0, capacity = 0).
   */
  inline SqliteValueTuple() : m_data(nullptr), m_size(0), m_capacity(0) {}

  /**
   * @brief Sized constructor. Allocates `size` elements on the heap and initializes via SIMD memcpy.
   * @param size Number of columns in this tuple.
   */
  inline explicit SqliteValueTuple(int size)
      : m_data(nullptr), m_size(0), m_capacity(0) {
    if (size > 0) {
      allocate_memory(static_cast<uint32_t>(size));
      m_size = static_cast<uint32_t>(size);
      init_null_values();
    }
  }

  // Synthesizes all primitive, array, and variadic heterogeneous constructors
  SQLITE_DERIVE_VEC_CONSTRUCTORS(SqliteValueTuple)

  /**
   * @brief Multi-column initializing constructor from SqliteRowView.
   * @param view_arr Source multi-column row view.
   */
  inline explicit SqliteValueTuple(const SqliteRowView &view_arr)
      : m_data(nullptr), m_size(0), m_capacity(0) {
    if (view_arr.size() > 0) {
      allocate_memory(static_cast<uint32_t>(view_arr.size()));
      m_size = static_cast<uint32_t>(view_arr.size());
      init_null_values();
      for (int i = 0; i < view_arr.size(); ++i) {
        m_data[i] = view_arr[i].to_owned();
      }
    }
  }

  /**
   * @brief Projecting constructor extracting indexed columns from a full row
   * container.
   * @tparam RowType Source container supporting `operator[](int)` and
   * `.size()`.
   * @param full_row Complete row containing all table columns.
   * @param col_indices Array of column indices to project. If null, uses
   * identity 0..count-1.
   * @param count Number of indices in col_indices.
   */
  template <typename RowType>
  inline SqliteValueTuple(const RowType &full_row, const int *col_indices,
                          int count)
      : m_data(nullptr), m_size(0), m_capacity(0) {
    if (count > 0) {
      allocate_memory(static_cast<uint32_t>(count));
      m_size = static_cast<uint32_t>(count);
      init_null_values();
      for (int i = 0; i < count; ++i) {
        int col = col_indices ? col_indices[i] : i;
        if (col >= 0 && col < full_row.size()) {
          m_data[i] = full_row[col].clone();
        }
      }
    }
  }

  /**
   * @brief Destructor. Destroys elements and frees heap buffer.
   */
  ~SqliteValueTuple() { release_memory(); }

  /**
   * @brief Deep copy constructor.
   * @param other Source tuple.
   */
  inline SqliteValueTuple(const SqliteValueTuple &other)
      : m_data(nullptr), m_size(0), m_capacity(0) {
    allocate_memory(other.m_size);
    m_size = other.m_size;
    for (uint32_t i = 0; i < m_size; ++i)
      sqlite_construct_at(&m_data[i], other.m_data[i].clone());
  }

  /**
   * @brief Deep copy assignment operator.
   * @param other Source tuple.
   * @return Reference to this tuple.
   */
  inline SqliteValueTuple &operator=(const SqliteValueTuple &other) {
    if (this != &other) {
      release_memory();
      allocate_memory(other.m_size);
      m_size = other.m_size;
      for (uint32_t i = 0; i < m_size; ++i)
        sqlite_construct_at(&m_data[i], other.m_data[i].clone());
    }
    return *this;
  }

  /**
   * @brief Move constructor. Transfers heap pointer without allocation.
   * @param other Source tuple.
   */
  inline SqliteValueTuple(SqliteValueTuple &&other) noexcept
      : m_data(other.m_data), m_size(other.m_size),
        m_capacity(other.m_capacity) {
    other.m_data = nullptr;
    other.m_size = 0;
    other.m_capacity = 0;
  }

  /**
   * @brief Move assignment operator.
   * @param other Source tuple.
   * @return Reference to this tuple.
   */
  inline SqliteValueTuple &operator=(SqliteValueTuple &&other) noexcept {
    if (this != &other) {
      release_memory();
      m_data = other.m_data;
      m_size = other.m_size;
      m_capacity = other.m_capacity;
      other.m_data = nullptr;
      other.m_size = 0;
      other.m_capacity = 0;
    }
    return *this;
  }

  // Standard C++ Container Type Definitions
  SQLITE_DERIVE_STANDARD_CONTAINER_TYPEDEFS(SqliteValueOwned, SqliteValueOwned&, const SqliteValueOwned&, SqliteValueOwned*, const SqliteValueOwned*, SqliteValueOwned*, const SqliteValueOwned*)

  /** @brief Active column count. */
  inline int size() const noexcept { return static_cast<int>(m_size); }

  /** @brief Alias for size() returning the fixed column count. */
  inline int count() const noexcept { return static_cast<int>(m_size); }

  /** @brief Alias for size() returning the column count. */
  inline int column_count() const noexcept { return static_cast<int>(m_size); }

  /** @brief Checks if the tuple is empty. */
  inline bool empty() const noexcept { return m_size == 0; }

  /** @brief Always false for N == 0 (resides on heap). */
  inline constexpr bool is_inline() const noexcept { return false; }

  /** @brief Always true for N == 0 (resides on heap). */
  inline constexpr bool is_heap() const noexcept { return true; }

  /** @brief Returns pointer to contiguous heap buffer. */
  inline pointer       data() noexcept { return m_data; }

  /** @brief Returns const pointer to contiguous heap buffer. */
  inline const_pointer data() const noexcept { return m_data; }

  /** @brief Resets all columns in the heap tuple to SQLITE_NULL. */
  inline void set_null_all() noexcept {
    for (uint32_t i = 0; i < m_size; ++i) {
      m_data[i].set_null();
    }
  }

  // Standard Array Accessors, Iterators, and Modifiers
  SQLITE_DERIVE_STD_ARRAY_METHODS(m_data, m_size, fallback_null(), m_size)
  SQLITE_DERIVE_STD_TUPLE_MODIFIERS(m_data, m_size)

  /** @brief Swaps the heap buffers of this tuple with another. */
  inline void swap(SqliteValueTuple& other) noexcept {
    SqliteValueOwned* tmp_ptr = m_data;
    uint32_t tmp_sz = m_size;
    uint32_t tmp_cap = m_capacity;
    m_data = other.m_data;
    m_size = other.m_size;
    m_capacity = other.m_capacity;
    other.m_data = tmp_ptr;
    other.m_size = tmp_sz;
    other.m_capacity = tmp_cap;
  }

private:
  static inline SqliteValueOwned &fallback_null() noexcept {
    return const_cast<SqliteValueOwned &>(SqliteValueOwned::static_null());
  }

public:
  /**
   * @brief Creates a lightweight non-owning span wrapper over the heap buffer.
   * @return 16-byte SqliteRowOwnedWrapper spanning m_data[0..size-1].
   */
  inline SqliteRowOwnedWrapper view() const noexcept {
    return SqliteRowOwnedWrapper(m_data, static_cast<int>(m_size));
  }

  /**
   * @brief Implicit conversion operator to non-owning row span wrapper.
   */
  inline operator SqliteRowOwnedWrapper() const noexcept { return view(); }

  /**
   * @brief Computes 64-bit composite hash code across all heap elements.
   * @return 64-bit MurmurHash2 hash value.
   */
  inline unsigned long long hash() const noexcept {
    if (m_size == 0)
      return SqliteHashUtil::DEFAULT_SEED;
    uint64_t h = SqliteHashUtil::DEFAULT_SEED;
    for (uint32_t i = 0; i < m_size; ++i) {
      h = SqliteHashUtil::combine(h, m_data[i].hash());
    }
    return h;
  }

  // Synthesized Typed Accessors & Full Relational Ops
  SQLITE_DERIVE_ARRAY_ACCESSORS
  SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteValueTuple)
  SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteRowOwnedWrapper)
  SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteRowView)
  SQLITE_DERIVE_ALL_SCALAR_RELATIONAL_OPS
};

// ============================================================================
// PART 2: SqliteValueVec<size_t N = 0> (Adaptive Vector with Full Heap Spill)
// ============================================================================

/**
 * @brief Primary template declaration for SqliteValueVec.
 * @tparam N Small Buffer Optimization inline capacity (default N = 0 for pure dynamic heap).
 * @tparam Enable SFINAE specialization selector.
 */
template <size_t N, typename Enable> class SqliteValueVec;

/**
 * @brief Specialization 1: N in [1..8] (In-Situ Stack SBO -> Completely Spills
 * to Heap on Overflow).
 *
 * Operates on the stack with zero heap allocation for element counts $\le N$.
 * When resized $> N$, it allocates a contiguous heap array via
 * `sqlite3_malloc64` and moves inline elements to the heap. When shrunk back
 * $\le N$, it returns to stack storage and frees the heap.
 *
 * @tparam N In-situ stack capacity between 1 and 8 inclusive.
 */
template <size_t N>
class SqliteValueVec<
    N, typename sqlite_container_enable_if<(N >= 1 && N <= 8)>::type> {
  static_assert(N >= 1 && N <= 8,
                "SqliteValueVec stack specialization is for N = 1..8");

private:
  /**
   * @brief Heap representation layout overlaying the in-situ stack union
   * buffer.
   */
  struct HeapRep {
    SqliteValueOwned *ptr; ///< 8 Bytes: Heap pointer (Offset 0..7).
    uint32_t size;         ///< 4 Bytes: Active element count (Offset 8..11).
    uint16_t capacity;     ///< 2 Bytes: Allocated capacity (Offset 12..13).
    uint8_t reserved;      ///< 1 Byte:  Reserved padding (Offset 14).
    SqliteOwnedValueTag
        tag; ///< 1 Byte:  Tag discriminator (Offset 15, tag.raw == 0x00).
    uint8_t pad[(N * 16) > 16 ? (N * 16) - 16
                              : 0]; ///< Padding to match N * 16 bytes.
  };
  static_assert(sizeof(HeapRep) == N * 16,
                "HeapRep must match union buffer size");

  union {
    SqliteValueOwned
        m_inline[N];  ///< Exact N * 16 Bytes: In-situ stack storage.
    HeapRep m_heap;   ///< Exact N * 16 Bytes: Dynamic heap control block.
    uint64_t m_align; ///< 8-byte alignment guarantee.
  };

  /** @brief Checks if the container currently holds heap-allocated storage. */
  inline bool is_heap() const noexcept {
    return m_heap.tag.is_heap_container(m_heap.ptr);
  }

  /** @brief Initializes the union to an empty, zero-initialized stack state. */
  inline void init_empty() noexcept {
    memset(static_cast<void *>(this), 0, sizeof(SqliteValueVec));
  }

  /** @brief Destroys active elements and releases heap memory if active. */
  inline void destroy_payload() noexcept {
    if (is_heap()) {
      if (m_heap.ptr) {
        sqlite_destroy_n(m_heap.ptr, m_heap.size);
        sqlite_delete_array(m_heap.ptr);
        m_heap.ptr = nullptr;
      }
      m_heap.size = 0;
    } else {
      int sz = size();
      for (int i = 0; i < sz; ++i) {
        m_inline[i].~SqliteValueOwned();
      }
      init_empty();
    }
  }

  /**
   * @brief Range initializer populating vector elements from contiguous source
   * array.
   *
   * @tparam TValueType Source type convertible to SqliteValueOwned.
   * @param src Pointer to contiguous array of source elements.
   * @param count Number of elements in src.
   */
  template <typename TValueType>
  inline void init_from_range(const TValueType *src, int count) {
    init_empty();
    if (src && count > 0) {
      resize(count);
      SqliteValueOwned *dst = data();
      for (int i = 0; i < count; ++i) {
        dst[i] = SqliteValueOwned(src[i]);
      }
    }
  }

public:
  /**
   * @brief Default constructor. Constructs an empty vector (size = 0) on the
   * stack.
   */
  inline SqliteValueVec() noexcept { init_empty(); }

  /**
   * @brief Sized constructor. Pre-allocates and default-constructs `count`
   * elements.
   * @param count Initial element count.
   */
  inline explicit SqliteValueVec(int count) {
    init_empty();
    resize(count);
  }

  // Synthesizes all primitive, array, and variadic heterogeneous constructors
  SQLITE_DERIVE_VEC_CONSTRUCTORS(SqliteValueVec)

  /**
   * @brief Multi-column initializing constructor from SqliteRowView.
   * @param view_arr Source row view copied into vector.
   */
  inline explicit SqliteValueVec(const SqliteRowView &view_arr) {
    init_empty();
    int count = view_arr.size();
    if (count > 0) {
      resize(count);
      SqliteValueOwned *dst = data();
      for (int i = 0; i < count; ++i) {
        dst[i] = view_arr[i].to_owned();
      }
    }
  }

  /**
   * @brief Destructor. Cleans up inline elements or heap allocation.
   */
  ~SqliteValueVec() { destroy_payload(); }

  /**
   * @brief Move constructor. Bitwise transfers state in 1 CPU cycle.
   * @param other Source vector.
   */
  inline SqliteValueVec(SqliteValueVec &&other) noexcept {
    memcpy(static_cast<void *>(this), static_cast<const void *>(&other),
           sizeof(SqliteValueVec));
    other.init_empty();
  }

  /**
   * @brief Move assignment operator.
   * @param other Source vector.
   * @return Reference to this vector.
   */
  inline SqliteValueVec &operator=(SqliteValueVec &&other) noexcept {
    if (this != &other) {
      destroy_payload();
      memcpy(static_cast<void *>(this), static_cast<const void *>(&other),
             sizeof(SqliteValueVec));
      other.init_empty();
    }
    return *this;
  }

  /**
   * @brief Deep copy constructor. Clones all active elements.
   * @param other Source vector.
   */
  inline SqliteValueVec(const SqliteValueVec &other) {
    init_empty();
    int sz = other.size();
    if (sz > 0) {
      resize(sz);
      const SqliteValueOwned *src = other.data();
      SqliteValueOwned *dst = data();
      for (int i = 0; i < sz; ++i) {
        dst[i] = src[i].clone();
      }
    }
  }

  /**
   * @brief Deep copy assignment operator.
   * @param other Source vector.
   * @return Reference to this vector.
   */
  inline SqliteValueVec &operator=(const SqliteValueVec &other) {
    if (this != &other) {
      destroy_payload();
      int sz = other.size();
      if (sz > 0) {
        resize(sz);
        const SqliteValueOwned *src = other.data();
        SqliteValueOwned *dst = data();
        for (int i = 0; i < sz; ++i) {
          dst[i] = src[i].clone();
        }
      }
    }
    return *this;
  }

  /**
   * @brief Returns current number of active elements stored in the vector.
   *
   * ### 100% Data Density Branchless Stack SBO Mechanics:
   * When residing on the stack (`!is_heap()`), `SqliteValueVec<N>` occupies
   * exactly `N * 16` bytes with zero bytes wasted on an external size integer.
   *
   * - `init_empty()` zeroes the entire buffer (`tag == 0x00`).
   * - Any constructed SQLite value has a valid type code in [1..5],
   * guaranteeing its tag is `raw >= 0x20` (`is_active() == true`).
   * - Size is computed branchlessly by summing boolean active states (`setae` /
   * `add`), eliminating conditional jumps and pipeline stalls.
   *
   * @return Number of active elements (0..N when inline, 0..m_heap.size when on
   * heap).
   */
  inline int size() const noexcept {
    if (is_heap())
      return static_cast<int>(m_heap.size);
    int sz = m_inline[0].is_active() ? 1 : 0;
    if (N >= 2)
      sz += (m_inline[1].is_active() ? 1 : 0);
    if (N >= 3)
      sz += (m_inline[2].is_active() ? 1 : 0);
    if (N >= 4)
      sz += (m_inline[3].is_active() ? 1 : 0);
    if (N >= 5)
      sz += (m_inline[4].is_active() ? 1 : 0);
    if (N >= 6)
      sz += (m_inline[5].is_active() ? 1 : 0);
    if (N >= 7)
      sz += (m_inline[6].is_active() ? 1 : 0);
    if (N >= 8)
      sz += (m_inline[7].is_active() ? 1 : 0);
    return sz;
  }

  /** @brief Alias for size() returning the active element count. */
  inline int count() const noexcept { return size(); }

  /** @brief Alias for size() returning the column count. */
  inline int column_count() const noexcept { return size(); }

  /** @brief Checks if the vector contains 0 active elements. */
  inline bool empty() const noexcept { return size() == 0; }

  /** @brief Returns true if elements are currently stored in-situ on the stack
   * (size <= N). */
  inline bool is_inline() const noexcept { return !is_heap(); }

  /** @brief Resets all active elements in the vector to SQLITE_NULL. */
  inline void set_null_all() noexcept {
    int sz = size();
    SqliteValueOwned *d = data();
    for (int i = 0; i < sz; ++i) {
      d[i].set_null();
    }
  }

  /**
   * @brief Contracts dynamic heap allocation back into in-situ stack array when
   * target <= N.
   * @param target Target element count (guaranteed <= N).
   */
  inline void resize_heap_to_stack(uint32_t target) {
    SqliteValueOwned *old_ptr = m_heap.ptr;
    uint32_t old_sz = m_heap.size;
    init_empty();

    uint32_t copy_cnt = (target < old_sz) ? target : old_sz;
    if (copy_cnt > 0 && old_ptr) {
      memcpy(static_cast<void *>(m_inline), static_cast<const void *>(old_ptr),
             copy_cnt * sizeof(SqliteValueOwned));
    }
    if (target > copy_cnt) {
      sqlite_construct_n(&m_inline[copy_cnt], target - copy_cnt);
    }
    if (old_ptr) {
      if (old_sz > copy_cnt) {
        sqlite_destroy_n(old_ptr + copy_cnt, old_sz - copy_cnt);
      }
      sqlite_delete_array(old_ptr);
    }
  }

  /**
   * @brief Expands or shrinks the in-situ stack array when remaining within
   * stack capacity N.
   * @param target Target element count (guaranteed <= N).
   * @param current_sz Current active element count on the stack.
   */
  inline void resize_inline_stack(uint32_t target, int current_sz) {
    if (target > static_cast<uint32_t>(current_sz)) {
      sqlite_construct_n(&m_inline[current_sz], target - current_sz);
    } else if (target < static_cast<uint32_t>(current_sz)) {
      sqlite_destroy_n(&m_inline[target], current_sz - target);
      memset(reinterpret_cast<void *>(&m_inline[target]), 0,
             (current_sz - target) * sizeof(SqliteValueOwned));
    }
  }

  /**
   * @brief Spills in-situ stack array to dynamic heap storage when target
   * exceeds N.
   * @param target Target element count (guaranteed > N).
   * @param current_sz Current active element count on the stack.
   */
  inline void spill_stack_to_heap(uint32_t target, int current_sz) {
    SqliteValueOwned *heap_buf =
        sqlite_new_array_zeroed<SqliteValueOwned>(target);
    if (current_sz > 0) {
      memcpy(static_cast<void *>(heap_buf), static_cast<const void *>(m_inline),
             current_sz * sizeof(SqliteValueOwned));
    }
    init_empty();

    m_heap.ptr = heap_buf;
    m_heap.capacity = static_cast<uint16_t>(target);
    m_heap.size = target;
    m_heap.tag.clear();

    if (target > static_cast<uint32_t>(current_sz)) {
      sqlite_construct_n(&m_heap.ptr[current_sz], target - current_sz);
    }
  }

  /**
   * @brief Reallocates, grows, or trims the existing dynamic heap buffer.
   * @param target Target element count (guaranteed > N).
   */
  inline void resize_heap_buffer(uint32_t target) {
    if (target > m_heap.capacity) {
      SqliteValueOwned *new_buf =
          sqlite_new_array_zeroed<SqliteValueOwned>(target);
      if (m_heap.size > 0 && m_heap.ptr) {
        memcpy(static_cast<void *>(new_buf),
               static_cast<const void *>(m_heap.ptr),
               m_heap.size * sizeof(SqliteValueOwned));
      }
      if (m_heap.ptr) {
        sqlite_delete_array(m_heap.ptr);
      }
      m_heap.ptr = new_buf;
      m_heap.capacity = static_cast<uint16_t>(target);
    }
    if (target > m_heap.size) {
      sqlite_construct_n(&m_heap.ptr[m_heap.size], target - m_heap.size);
    } else if (target < m_heap.size) {
      sqlite_destroy_n(m_heap.ptr + target, m_heap.size - target);
    }
    m_heap.size = target;
    m_heap.tag.clear();
  }

public:
  /**
   * @brief Resizes vector to new_count, spilling to heap if > N or returning to
   * stack if <= N.
   * @param new_count Target element count.
   */
  inline void resize(int new_count) {
    if (new_count < 0)
      new_count = 0;
    uint32_t target = static_cast<uint32_t>(new_count);
    int current_sz = size();

    if (target <= N) {
      if (is_heap()) {
        resize_heap_to_stack(target);
      } else {
        resize_inline_stack(target, current_sz);
      }
    } else {
      if (!is_heap()) {
        spill_stack_to_heap(target, current_sz);
      } else {
        resize_heap_buffer(target);
      }
    }
  }

  /**
   * @brief Clears all elements from the vector (size becomes 0).
   *
   * Resets the active element count to 0. For stack storage, properly executes
   * element destructors and zero-initializes the SBO union buffer. For heap
   * storage, preserves the allocated buffer capacity to eliminate future
   * allocation overhead.
   */
  inline void clear() noexcept { resize(0); }

  /**
   * @brief Returns currently allocated buffer capacity.
   *
   * @return For stack storage, returns the fixed in-situ template capacity `N`.
   *         For heap storage, returns the dynamic buffer capacity allocated via
   * `sqlite3_malloc64`.
   */
  inline int capacity() const noexcept {
    return is_heap() ? static_cast<int>(m_heap.capacity) : static_cast<int>(N);
  }

  /**
   * @brief Pre-allocates buffer capacity for at least `new_cap` elements.
   *
   * ### Memory & Reallocation Strategy:
   * - If `new_cap <= capacity()`, this operation is a strict no-op.
   * - If currently on stack and `new_cap > N`, transitions from stack to
   * dynamic heap storage.
   * - If already on heap and `new_cap > capacity()`, allocates a new buffer via
   * `sqlite3_malloc64`, moves existing elements, and frees the old heap buffer.
   *
   * ### Zero-Initialization Guarantee:
   * All unconstructed spare capacity slots between `size()` and `target_cap`
   * are explicitly zero-initialized with `memset` to guarantee that memory
   * sanitizers (ASan/Valgrind) observe clean `0x00` bytes and inactive tags.
   *
   * @param new_cap Target minimum capacity.
   */
  inline void reserve(int new_cap) {
    if (new_cap <= capacity())
      return;
    int current_sz = size();
    uint32_t target_cap = static_cast<uint32_t>(new_cap);

    if (!is_heap()) {
      SqliteValueOwned *heap_buf =
          sqlite_new_array_zeroed<SqliteValueOwned>(target_cap);
      if (current_sz > 0) {
        memcpy(static_cast<void *>(heap_buf),
               static_cast<const void *>(m_inline),
               current_sz * sizeof(SqliteValueOwned));
      }
      init_empty();

      m_heap.ptr = heap_buf;
      m_heap.capacity = static_cast<uint16_t>(target_cap);
      m_heap.size = static_cast<uint32_t>(current_sz);
      m_heap.tag.clear();
    } else {
      SqliteValueOwned *new_buf =
          sqlite_new_array_zeroed<SqliteValueOwned>(target_cap);
      if (m_heap.size > 0 && m_heap.ptr) {
        memcpy(static_cast<void *>(new_buf),
               static_cast<const void *>(m_heap.ptr),
               m_heap.size * sizeof(SqliteValueOwned));
      }
      if (m_heap.ptr) {
        sqlite_delete_array(m_heap.ptr);
      }
      m_heap.ptr = new_buf;
      m_heap.capacity = static_cast<uint16_t>(target_cap);
    }
  }

  /**
   * @brief Appends an element to the end of the vector by copy.
   *
   * ### Growth Strategy:
   * When `size() >= capacity()`, automatically expands buffer capacity
   * geometrically ($2\times$) via `reserve()`. If initially on stack, seamless
   * SBO spilling relocates inline elements to the heap.
   *
   * @param val Value to clone and append.
   */
  inline void push_back(const SqliteValueOwned &val) {
    int sz = size();
    if (sz >= capacity()) {
      int new_cap = (sz == 0)
                        ? (static_cast<int>(N) > 0 ? static_cast<int>(N) : 4)
                        : sz * 2;
      if (new_cap <= static_cast<int>(N))
        new_cap = static_cast<int>(N) + 1;
      reserve(new_cap);
    }
    if (!is_heap()) {
      sqlite_construct_at(&m_inline[sz], val.clone());
    } else {
      sqlite_construct_at(&m_heap.ptr[sz], val.clone());
      m_heap.size++;
    }
  }

  /**
   * @brief Appends an element to the end of the vector by move.
   *
   * Transfers ownership of heap-backed strings/blobs with zero copying
   * overhead.
   *
   * @param val Rvalue reference to value to move and append.
   */
  inline void push_back(SqliteValueOwned &&val) {
    int sz = size();
    if (sz >= capacity()) {
      int new_cap = (sz == 0)
                        ? (static_cast<int>(N) > 0 ? static_cast<int>(N) : 4)
                        : sz * 2;
      if (new_cap <= static_cast<int>(N))
        new_cap = static_cast<int>(N) + 1;
      reserve(new_cap);
    }
    if (!is_heap()) {
      sqlite_construct_at(&m_inline[sz], sqlite_move(val));
    } else {
      sqlite_construct_at(&m_heap.ptr[sz], sqlite_move(val));
      m_heap.size++;
    }
  }

  /**
   * @brief Appends a primitive or convertible value to the end of the vector.
   *
   * Enables direct appending of integers, floats, strings, string views, blobs,
   * and booleans:
   * ```cpp
   * vec.push_back(42);
   * vec.push_back(3.14);
   * vec.push_back("hello");
   * vec.push_back(true);
   * ```
   *
   * @tparam TValueType Source type convertible to SqliteValueOwned.
   * @param val Value to convert and append.
   */
  template <typename TValueType> inline void push_back(TValueType &&val) {
    push_back(SqliteValueOwned(sqlite_forward<TValueType>(val)));
  }

  /**
   * @brief In-place constructs a new element at the end of the vector.
   *
   * Forwards constructor arguments directly into `SqliteValueOwned`
   * constructor, avoiding any intermediate temporary objects.
   *
   * @tparam Args Constructor argument types.
   * @param args Constructor arguments forwarded to SqliteValueOwned.
   */
  template <typename... Args> inline void emplace_back(Args &&...args) {
    push_back(SqliteValueOwned(sqlite_forward<Args>(args)...));
  }

  /**
   * @brief Removes the last element from the vector in O(1) time.
   *
   * Properly invokes the trailing element's destructor. If the vector is empty,
   * this operation safely performs no action.
   */
  inline void pop_back() noexcept {
    int sz = size();
    if (sz > 0) {
      resize(sz - 1);
    }
  }

  // Standard C++ Container Type Definitions
  SQLITE_DERIVE_STANDARD_CONTAINER_TYPEDEFS(SqliteValueOwned, SqliteValueOwned&, const SqliteValueOwned&, SqliteValueOwned*, const SqliteValueOwned*, SqliteValueOwned*, const SqliteValueOwned*)

  /** @brief Returns pointer to contiguous active element buffer (stack or
   * heap). */
  inline pointer       data() noexcept {
    return is_heap() ? m_heap.ptr : m_inline;
  }

  /** @brief Returns const pointer to contiguous active element buffer (stack or
   * heap). */
  inline const_pointer data() const noexcept {
    return is_heap() ? m_heap.ptr : m_inline;
  }

  // Standard Vector Iterators, Accessors, and Modifiers
  SQLITE_DERIVE_ARRAY_ITERATORS(data(), size())
  SQLITE_DERIVE_ARRAY_ELEMENT_ACCESSORS(data(), size(), fallback_null())
  SQLITE_DERIVE_STD_VEC_METHODS(SqliteValueVec)

  /** @brief Shrinks capacity to fit actual element count. */
  inline void shrink_to_fit() {
    int sz = size();
    if (is_heap() && sz <= static_cast<int>(N)) {
      resize_heap_to_stack(static_cast<uint32_t>(sz));
    } else if (is_heap() && sz < static_cast<int>(m_heap.capacity)) {
      SqliteValueOwned* new_buf = sqlite_new_array_zeroed<SqliteValueOwned>(sz);
      if (sz > 0 && m_heap.ptr) {
        memcpy(static_cast<void*>(new_buf), static_cast<const void*>(m_heap.ptr), sz * sizeof(SqliteValueOwned));
      }
      if (m_heap.ptr) sqlite_delete_array(m_heap.ptr);
      m_heap.ptr = new_buf;
      m_heap.capacity = static_cast<uint16_t>(sz);
    }
  }

private:
  static inline SqliteValueOwned& fallback_null() noexcept {
    return const_cast<SqliteValueOwned&>(SqliteValueOwned::static_null());
  }

public:
  /**
   * @brief Creates a lightweight non-owning span wrapper over this vector.
   * @return 16-byte SqliteRowOwnedWrapper spanning data()[0..size-1].
   */
  inline SqliteRowOwnedWrapper view() const noexcept {
    return SqliteRowOwnedWrapper(data(), size());
  }

  /**
   * @brief Implicit conversion operator to non-owning row span wrapper.
   */
  inline operator SqliteRowOwnedWrapper() const noexcept { return view(); }

  /**
   * @brief Computes 64-bit composite hash code for hash table lookups (Swiss
   * tables).
   * @return 64-bit MurmurHash2 hash value.
   */
  inline unsigned long long hash() const noexcept {
    int sz = size();
    if (sz == 1 && !is_heap())
      return m_inline[0].hash();
    if (sz == 0)
      return SqliteHashUtil::DEFAULT_SEED;

    const SqliteValueOwned *buf = data();
    uint64_t h = SqliteHashUtil::DEFAULT_SEED;
    for (int i = 0; i < sz; ++i) {
      h = SqliteHashUtil::combine(h, buf[i].hash());
    }
    return h;
  }

  // Synthesized Typed Accessors & Full Relational Ops
  SQLITE_DERIVE_ARRAY_ACCESSORS
  SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteValueVec)
  SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteRowOwnedWrapper)
  SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteRowView)
    SQLITE_DERIVE_ALL_SCALAR_RELATIONAL_OPS
};

/**
 * @brief Specialization 2: N == 0 (Direct Dynamic Heap Vector).
 *
 * For vector base capacity N == 0 (default unbounded dynamic vector),
 * elements are always allocated dynamically on the heap with 0 SBO stack union overhead.
 *
 * @tparam N Base capacity 0.
 */
template <size_t N>
class SqliteValueVec<
    N, typename sqlite_container_enable_if<(N == 0)>::type> {
private:
  SqliteValueOwned *m_data =
      nullptr; ///< Contiguous heap buffer allocated via sqlite3_malloc64.
  uint32_t m_size = 0;     ///< Current active element count.
  uint32_t m_capacity = 0; ///< Allocated capacity.

  inline void release_memory() noexcept {
    if (m_data) {
      sqlite_destroy_n(m_data, m_size);
      sqlite_delete_array(m_data);
      m_data = nullptr;
    }
    m_size = 0;
    m_capacity = 0;
  }

  inline void allocate_memory(uint32_t cap) {
    release_memory();
    if (cap > 0) {
      m_data = sqlite_new_array<SqliteValueOwned>(cap);
      m_capacity = m_data ? cap : 0;
    }
  }

  template <typename TValueType>
  inline void init_from_range(const TValueType *src, int count) {
    if (src && count > 0) {
      resize(count);
      for (int i = 0; i < count; ++i) {
        m_data[i] = SqliteValueOwned(src[i]);
      }
    }
  }

public:
  /**
   * @brief Default constructor. Constructs an empty vector on the heap (size = 0, capacity = 0).
   */
  inline SqliteValueVec() : m_data(nullptr), m_size(0), m_capacity(0) {}

  /**
   * @brief Sized constructor. Pre-allocates and constructs `count` elements.
   * @param count Initial element count.
   */
  inline explicit SqliteValueVec(int count)
      : m_data(nullptr), m_size(0), m_capacity(0) {
    resize(count);
  }

  SQLITE_DERIVE_VEC_CONSTRUCTORS(SqliteValueVec)

  /**
   * @brief Destructor. Destroys elements and frees heap memory.
   */
  ~SqliteValueVec() { release_memory(); }

  /**
   * @brief Deep copy constructor.
   * @param other Source vector.
   */
  inline SqliteValueVec(const SqliteValueVec &other)
      : m_data(nullptr), m_size(0), m_capacity(0) {
    if (other.m_size > 0) {
      allocate_memory(other.m_size);
      m_size = other.m_size;
      for (uint32_t i = 0; i < m_size; ++i)
        sqlite_construct_at(&m_data[i], other.m_data[i].clone());
    }
  }

  /**
   * @brief Deep copy assignment operator.
   * @param other Source vector.
   * @return Reference to this vector.
   */
  inline SqliteValueVec &operator=(const SqliteValueVec &other) {
    if (this != &other) {
      release_memory();
      if (other.m_size > 0) {
        allocate_memory(other.m_size);
        m_size = other.m_size;
        for (uint32_t i = 0; i < m_size; ++i)
          sqlite_construct_at(&m_data[i], other.m_data[i].clone());
      }
    }
    return *this;
  }

  /**
   * @brief Move constructor. Transfers heap pointer in 1 CPU cycle.
   * @param other Source vector.
   */
  inline SqliteValueVec(SqliteValueVec &&other) noexcept
      : m_data(other.m_data), m_size(other.m_size),
        m_capacity(other.m_capacity) {
    other.m_data = nullptr;
    other.m_size = 0;
    other.m_capacity = 0;
  }

  /**
   * @brief Move assignment operator.
   * @param other Source vector.
   * @return Reference to this vector.
   */
  inline SqliteValueVec &operator=(SqliteValueVec &&other) noexcept {
    if (this != &other) {
      release_memory();
      m_data = other.m_data;
      m_size = other.m_size;
      m_capacity = other.m_capacity;
      other.m_data = nullptr;
      other.m_size = 0;
      other.m_capacity = 0;
    }
    return *this;
  }

  /** @brief Active element count. */
  inline int size() const noexcept { return static_cast<int>(m_size); }

  /** @brief Alias for size() returning the active element count. */
  inline int count() const noexcept { return static_cast<int>(m_size); }

  /** @brief Alias for size() returning the column count. */
  inline int column_count() const noexcept { return static_cast<int>(m_size); }

  /** @brief Checks if the vector contains 0 active elements. */
  inline bool empty() const noexcept { return m_size == 0; }

  /** @brief Always false for N == 0 (resides on heap). */
  inline constexpr bool is_inline() const noexcept { return false; }

  /** @brief Always true for N == 0 (resides on heap). */
  inline constexpr bool is_heap() const noexcept { return true; }

  /** @brief Resets all active elements in the heap vector to SQLITE_NULL. */
  inline void set_null_all() noexcept {
    for (uint32_t i = 0; i < m_size; ++i) {
      m_data[i].set_null();
    }
  }

  /**
   * @brief Dynamically resizes vector buffer via sqlite3_malloc64 /
   * sqlite3_free.
   * @param new_count Target element count.
   */
  inline void resize(int new_count) {
    if (new_count < 0)
      new_count = 0;
    uint32_t target = static_cast<uint32_t>(new_count);
    if (target == m_size)
      return;
    if (target == 0) {
      release_memory();
      return;
    }

    if (target > m_capacity) {
      SqliteValueOwned *new_buf =
          sqlite_new_array_zeroed<SqliteValueOwned>(target);
      if (m_size > 0 && m_data) {
        memcpy(static_cast<void *>(new_buf), static_cast<const void *>(m_data),
               m_size * sizeof(SqliteValueOwned));
      }
      if (m_data) {
        sqlite_delete_array(m_data);
      }
      m_data = new_buf;
      m_capacity = target;
    }

    if (target > m_size) {
      uint32_t needed = target - m_size;
      uint32_t i = 0;
      while (i + 8 <= needed) {
        memcpy(static_cast<void *>(m_data + m_size + i),
               static_cast<const void *>(SqliteValueOwned::static_null_array()),
               8 * sizeof(SqliteValueOwned));
        i += 8;
      }
      if (i < needed) {
        memcpy(static_cast<void *>(m_data + m_size + i),
               static_cast<const void *>(SqliteValueOwned::static_null_array()),
               (needed - i) * sizeof(SqliteValueOwned));
      }
    } else if (target < m_size) {
      sqlite_destroy_n(m_data + target, m_size - target);
    }
    m_size = target;
  }

  /**
   * @brief Clears all elements from the heap vector (size becomes 0).
   *
   * Destructs all active elements and resets the active count to 0, preserving
   * the allocated heap capacity to prevent dynamic reallocations during
   * subsequent insertions.
   */
  inline void clear() noexcept { resize(0); }

  /**
   * @brief Returns currently allocated buffer capacity.
   * @return Number of elements the heap buffer can hold before requiring
   * reallocation.
   */
  inline int capacity() const noexcept { return static_cast<int>(m_capacity); }

  /**
   * @brief Pre-allocates heap buffer capacity for at least `new_cap` elements
   * via sqlite3_malloc64.
   *
   * ### Zero-Initialization Guarantee:
   * All unconstructed spare capacity slots between `size()` and `target_cap`
   * are explicitly zero-initialized with `memset` to guarantee that memory
   * sanitizers (ASan/Valgrind) observe clean `0x00` bytes and inactive tags.
   *
   * @param new_cap Target minimum capacity.
   */
  inline void reserve(int new_cap) {
    if (new_cap <= static_cast<int>(m_capacity))
      return;
    uint32_t target_cap = static_cast<uint32_t>(new_cap);
    SqliteValueOwned *new_buf =
        sqlite_new_array_zeroed<SqliteValueOwned>(target_cap);
    if (m_size > 0 && m_data) {
      memcpy(static_cast<void *>(new_buf), static_cast<const void *>(m_data),
             m_size * sizeof(SqliteValueOwned));
    }
    if (m_data) {
      sqlite_delete_array(m_data);
    }
    m_data = new_buf;
    m_capacity = target_cap;
  }

  /**
   * @brief Appends an element to the end of the heap vector by copy.
   *
   * Automatically expands capacity geometrically ($2\times$) via `reserve()`
   * when full.
   *
   * @param val Value to clone and append.
   */
  inline void push_back(const SqliteValueOwned &val) {
    if (m_size >= m_capacity) {
      int new_cap = (m_capacity == 0) ? 8 : static_cast<int>(m_capacity * 2);
      reserve(new_cap);
    }
    sqlite_construct_at(&m_data[m_size], val.clone());
    m_size++;
  }

  /**
   * @brief Appends an element to the end of the heap vector by move.
   *
   * Transfers ownership of heap-backed strings/blobs with zero copying
   * overhead.
   *
   * @param val Rvalue reference to value to move and append.
   */
  inline void push_back(SqliteValueOwned &&val) {
    if (m_size >= m_capacity) {
      int new_cap = (m_capacity == 0) ? 8 : static_cast<int>(m_capacity * 2);
      reserve(new_cap);
    }
    sqlite_construct_at(&m_data[m_size], sqlite_move(val));
    m_size++;
  }

  /**
   * @brief Appends a primitive or convertible value to the end of the heap
   * vector.
   *
   * @tparam TValueType Source type convertible to SqliteValueOwned.
   * @param val Value to convert and append.
   */
  template <typename TValueType> inline void push_back(TValueType &&val) {
    push_back(SqliteValueOwned(sqlite_forward<TValueType>(val)));
  }

  /**
   * @brief In-place constructs a new element at the end of the heap vector.
   *
   * @tparam Args Constructor argument types.
   * @param args Constructor arguments forwarded to SqliteValueOwned.
   */
  template <typename... Args> inline void emplace_back(Args &&...args) {
    push_back(SqliteValueOwned(sqlite_forward<Args>(args)...));
  }

  /**
   * @brief Removes the last element from the heap vector in O(1) time.
   */
  inline void pop_back() noexcept {
    if (m_size > 0) {
      resize(static_cast<int>(m_size - 1));
    }
  }

  // Standard C++ Container Type Definitions
  SQLITE_DERIVE_STANDARD_CONTAINER_TYPEDEFS(SqliteValueOwned, SqliteValueOwned&, const SqliteValueOwned&, SqliteValueOwned*, const SqliteValueOwned*, SqliteValueOwned*, const SqliteValueOwned*)

  /** @brief Returns pointer to contiguous heap buffer. */
  inline pointer       data() noexcept { return m_data; }

  /** @brief Returns const pointer to contiguous heap buffer. */
  inline const_pointer data() const noexcept { return m_data; }

  // Standard Vector Iterators, Accessors, and Modifiers
  SQLITE_DERIVE_ARRAY_ITERATORS(data(), size())
  SQLITE_DERIVE_ARRAY_ELEMENT_ACCESSORS(data(), size(), fallback_null())
  SQLITE_DERIVE_STD_VEC_METHODS(SqliteValueVec)

  /** @brief Shrinks capacity to fit actual element count. */
  inline void shrink_to_fit() {
    if (m_capacity > m_size) {
      if (m_size == 0) {
        release_memory();
      } else {
        SqliteValueOwned* new_buf = sqlite_new_array_zeroed<SqliteValueOwned>(m_size);
        if (m_data) {
          memcpy(static_cast<void*>(new_buf), static_cast<const void*>(m_data), m_size * sizeof(SqliteValueOwned));
          sqlite_delete_array(m_data);
        }
        m_data = new_buf;
        m_capacity = m_size;
      }
    }
  }

private:
  static inline SqliteValueOwned &fallback_null() noexcept {
    return const_cast<SqliteValueOwned &>(SqliteValueOwned::static_null());
  }

public:
  /**
   * @brief Creates a lightweight non-owning span wrapper over the heap buffer.
   * @return 16-byte SqliteRowOwnedWrapper spanning m_data[0..size-1].
   */
  inline SqliteRowOwnedWrapper view() const noexcept {
    return SqliteRowOwnedWrapper(m_data, static_cast<int>(m_size));
  }

  /**
   * @brief Implicit conversion operator to non-owning row span wrapper.
   */
  inline operator SqliteRowOwnedWrapper() const noexcept { return view(); }

  /**
   * @brief Computes 64-bit composite hash code across all heap elements.
   * @return 64-bit MurmurHash2 hash value.
   */
  inline unsigned long long hash() const noexcept {
    if (m_size == 0)
      return SqliteHashUtil::DEFAULT_SEED;
    uint64_t h = SqliteHashUtil::DEFAULT_SEED;
    for (uint32_t i = 0; i < m_size; ++i) {
      h = SqliteHashUtil::combine(h, m_data[i].hash());
    }
    return h;
  }

  // Synthesized Typed Accessors & Full Relational Ops
  SQLITE_DERIVE_ARRAY_ACCESSORS
  SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteValueVec)
  SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteRowOwnedWrapper)
  SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteRowView)
  SQLITE_DERIVE_ALL_SCALAR_RELATIONAL_OPS
};

// ============================================================================
// PART 3: Static Footprint Verifications
// ============================================================================

static_assert(sizeof(SqliteValueTuple<0>) == 16,
              "SqliteValueTuple<0> must be 16 bytes (ptr + size + capacity)!");
static_assert(sizeof(SqliteValueTuple<>) == 16,
              "SqliteValueTuple<> must be 16 bytes (default pure heap)!");
static_assert(sizeof(SqliteValueTuple<1>) == 16,
              "SqliteValueTuple<1> must be 16 bytes!");
static_assert(sizeof(SqliteValueTuple<2>) == 32,
              "SqliteValueTuple<2> must be 32 bytes!");
static_assert(sizeof(SqliteValueTuple<4>) == 64,
              "SqliteValueTuple<4> must be 64 bytes (1 L1 Line)!");
static_assert(sizeof(SqliteValueTuple<8>) == 128,
              "SqliteValueTuple<8> must be 128 bytes (2 L1 Lines)!");

static_assert(sizeof(SqliteValueVec<0>) == 16,
              "SqliteValueVec<0> must be 16 bytes (ptr + size + capacity)!");
static_assert(sizeof(SqliteValueVec<>) == 16,
              "SqliteValueVec<> must be 16 bytes (default pure heap)!");
static_assert(sizeof(SqliteValueVec<1>) == 16,
              "SqliteValueVec<1> must be 16 bytes!");
static_assert(sizeof(SqliteValueVec<2>) == 32,
              "SqliteValueVec<2> must be 32 bytes!");
static_assert(sizeof(SqliteValueVec<4>) == 64,
              "SqliteValueVec<4> must be 64 bytes (1 L1 Line)!");
static_assert(sizeof(SqliteValueVec<8>) == 128,
              "SqliteValueVec<8> must be 128 bytes (2 L1 Lines)!");

// ============================================================================
// PART 4: 1D Generic Compile-Time Dispatcher (1..8 + Default Heap Fallback N = 0 / <>)
// ============================================================================

/**
 * @def SQLITE_DISPATCH_1D_8
 * @brief Dispatches a runtime count (1..8) to a compile-time constexpr size_t 'N' (1..8).
 *        Counts outside [1..8] (e.g. 0 or > 8) dispatch to N = 0 (direct dynamic heap type SqliteValueTuple<> / SqliteValueVec<>).
 * 
 * Usage:
 *   SQLITE_DISPATCH_1D_8(ColsN, runtime_count, {
 *       return sqlite_new<MyContainer<SqliteValueTuple<ColsN>>>(arg1, arg2);
 *   });
 */
#define SQLITE_DISPATCH_1D_8(N, runtime_count, ...) \
    switch (runtime_count) { \
        case 1:  { constexpr size_t N = 1; __VA_ARGS__; } break; \
        case 2:  { constexpr size_t N = 2; __VA_ARGS__; } break; \
        case 3:  { constexpr size_t N = 3; __VA_ARGS__; } break; \
        case 4:  { constexpr size_t N = 4; __VA_ARGS__; } break; \
        case 5:  { constexpr size_t N = 5; __VA_ARGS__; } break; \
        case 6:  { constexpr size_t N = 6; __VA_ARGS__; } break; \
        case 7:  { constexpr size_t N = 7; __VA_ARGS__; } break; \
        case 8:  { constexpr size_t N = 8; __VA_ARGS__; } break; \
        default: { constexpr size_t N = 0; __VA_ARGS__; } break; /* Dynamic Heap Fallback (N = 0 / <>) */ \
    }

// ============================================================================
// PART 5: 2D Generic Compile-Time Matrix Dispatcher (8x8 = 64 Matrix Combinations)
// ============================================================================

/**
 * @def SQLITE_DISPATCH_2D_8X8
 * @brief Dispatches runtime key_count (1..8) and val_count (1..8) to compile-time 
 *        constexpr size_t 'KeyN' and 'ValN' variables inside the provided block.
 *        Counts outside [1..8] dispatch to KeyN = 0 / ValN = 0 (direct dynamic heap types).
 * 
 * Works with ANY container, template types, or custom factory logic.
 * 
 * Usage:
 *   SQLITE_DISPATCH_2D_8X8(KeyN, ValN, pk_count, val_count, {
 *       return sqlite_new<MyContainer<SqliteValueTuple<KeyN>, SqliteValueVec<ValN>>>(args...);
 *   });
 */
#define SQLITE_DISPATCH_2D_8X8(KeyN, ValN, pk_count, val_count, ...) \
    switch (pk_count) { \
        case 1:  { constexpr size_t KeyN = 1; SQLITE_DISPATCH_1D_8(ValN, val_count, __VA_ARGS__) } break; \
        case 2:  { constexpr size_t KeyN = 2; SQLITE_DISPATCH_1D_8(ValN, val_count, __VA_ARGS__) } break; \
        case 3:  { constexpr size_t KeyN = 3; SQLITE_DISPATCH_1D_8(ValN, val_count, __VA_ARGS__) } break; \
        case 4:  { constexpr size_t KeyN = 4; SQLITE_DISPATCH_1D_8(ValN, val_count, __VA_ARGS__) } break; \
        case 5:  { constexpr size_t KeyN = 5; SQLITE_DISPATCH_1D_8(ValN, val_count, __VA_ARGS__) } break; \
        case 6:  { constexpr size_t KeyN = 6; SQLITE_DISPATCH_1D_8(ValN, val_count, __VA_ARGS__) } break; \
        case 7:  { constexpr size_t KeyN = 7; SQLITE_DISPATCH_1D_8(ValN, val_count, __VA_ARGS__) } break; \
        case 8:  { constexpr size_t KeyN = 8; SQLITE_DISPATCH_1D_8(ValN, val_count, __VA_ARGS__) } break; \
        default: { constexpr size_t KeyN = 0; SQLITE_DISPATCH_1D_8(ValN, val_count, __VA_ARGS__) } break; \
    }

// ============================================================================
// PART 6: Shorthand Factory Macros (Direct sqlite_new Instantiation with Templates)
// ============================================================================

/**
 * @def SQLITE_MAKE_STORAGE_1D_8
 * @brief Generic 1D instantiator passing explicit Container and Value templates.
 */
#define SQLITE_MAKE_STORAGE_1D_8(ContainerT, ValT, count, ...) \
    SQLITE_DISPATCH_1D_8(_V_N, count, { \
        return sqlite_new<ContainerT<ValT<_V_N>>>(__VA_ARGS__); \
    })

/**
 * @def SQLITE_MAKE_DEFAULT_STORAGE_1D_8
 * @brief Shorthand 1D instantiator using standard SqliteValueTuple for columns.
 */
#define SQLITE_MAKE_DEFAULT_STORAGE_1D_8(ContainerT, count, ...) \
    SQLITE_MAKE_STORAGE_1D_8(ContainerT, SqliteValueTuple, count, __VA_ARGS__)

/**
 * @def SQLITE_MAKE_DEFAULT_VEC_STORAGE_1D_8
 * @brief Shorthand 1D instantiator using standard SqliteValueVec for columns.
 */
#define SQLITE_MAKE_DEFAULT_VEC_STORAGE_1D_8(ContainerT, count, ...) \
    SQLITE_MAKE_STORAGE_1D_8(ContainerT, SqliteValueVec, count, __VA_ARGS__)

/**
 * @def SQLITE_MAKE_STORAGE_8X8
 * @brief Generic 8x8 instantiator passing explicit Container, Key, and Value templates.
 */
#define SQLITE_MAKE_STORAGE_8X8(ContainerT, KeyT, ValT, pk_count, val_count, ...) \
    SQLITE_DISPATCH_2D_8X8(_K_N, _V_N, pk_count, val_count, { \
        return sqlite_new<ContainerT<KeyT<_K_N>, ValT<_V_N>>>(__VA_ARGS__); \
    })

/**
 * @def SQLITE_MAKE_DEFAULT_STORAGE_8X8
 * @brief Shorthand 8x8 instantiator using standard SqliteValueTuple for Key and SqliteValueVec for Value.
 */
#define SQLITE_MAKE_DEFAULT_STORAGE_8X8(ContainerT, pk_count, val_count, ...) \
    SQLITE_MAKE_STORAGE_8X8(ContainerT, SqliteValueTuple, SqliteValueVec, pk_count, val_count, __VA_ARGS__)

/**
 * @def SQLITE_MAKE_DEFAULT_TUPLE_STORAGE_8X8
 * @brief Shorthand 8x8 instantiator using SqliteValueTuple for both Key and Value.
 */
#define SQLITE_MAKE_DEFAULT_TUPLE_STORAGE_8X8(ContainerT, pk_count, val_count, ...) \
    SQLITE_MAKE_STORAGE_8X8(ContainerT, SqliteValueTuple, SqliteValueTuple, pk_count, val_count, __VA_ARGS__)

/**
 * @def SQLITE_MAKE_DEFAULT_VEC_STORAGE_8X8
 * @brief Shorthand 8x8 instantiator using SqliteValueVec for both Key and Value.
 */
#define SQLITE_MAKE_DEFAULT_VEC_STORAGE_8X8(ContainerT, pk_count, val_count, ...) \
    SQLITE_MAKE_STORAGE_8X8(ContainerT, SqliteValueVec, SqliteValueVec, pk_count, val_count, __VA_ARGS__)

// ============================================================================
// PART 7: Row Wrapper Scope Dispatch Macros (Direct SqliteRowOwnedWrapper Spans)
// ============================================================================

/**
 * @def SQLITE_WITH_ROW_OWNED_1D
 * @brief Dispatches runtime count (1..8) to a stack-allocated SqliteValueTuple<N> (or heap SqliteValueTuple<> if > 8),
 *        providing a SqliteRowOwnedWrapper span with the specified identifier inside the block without requiring a wrapper class.
 * 
 * Usage:
 *   SQLITE_WITH_ROW_OWNED_1D(row, num_cols, {
 *       row[0] = 100;
 *       row[1] = "sensor";
 *       process_row(row);
 *   });
 */
#define SQLITE_WITH_ROW_OWNED_1D(RowWrapperVar, runtime_count, ...) \
    do { \
        const int _sz = static_cast<int>(runtime_count); \
        switch (_sz) { \
            case 1: { SqliteValueTuple<1> _arr; SqliteRowOwnedWrapper RowWrapperVar(_arr.data(), 1); __VA_ARGS__; } break; \
            case 2: { SqliteValueTuple<2> _arr; SqliteRowOwnedWrapper RowWrapperVar(_arr.data(), 2); __VA_ARGS__; } break; \
            case 3: { SqliteValueTuple<3> _arr; SqliteRowOwnedWrapper RowWrapperVar(_arr.data(), 3); __VA_ARGS__; } break; \
            case 4: { SqliteValueTuple<4> _arr; SqliteRowOwnedWrapper RowWrapperVar(_arr.data(), 4); __VA_ARGS__; } break; \
            case 5: { SqliteValueTuple<5> _arr; SqliteRowOwnedWrapper RowWrapperVar(_arr.data(), 5); __VA_ARGS__; } break; \
            case 6: { SqliteValueTuple<6> _arr; SqliteRowOwnedWrapper RowWrapperVar(_arr.data(), 6); __VA_ARGS__; } break; \
            case 7: { SqliteValueTuple<7> _arr; SqliteRowOwnedWrapper RowWrapperVar(_arr.data(), 7); __VA_ARGS__; } break; \
            case 8: { SqliteValueTuple<8> _arr; SqliteRowOwnedWrapper RowWrapperVar(_arr.data(), 8); __VA_ARGS__; } break; \
            default: { \
                if (_sz <= 0) { \
                    SqliteRowOwnedWrapper RowWrapperVar(nullptr, 0); \
                    __VA_ARGS__; \
                } else { \
                    SqliteValueTuple<> _arr(_sz); \
                    SqliteRowOwnedWrapper RowWrapperVar(_arr.data(), _sz); \
                    __VA_ARGS__; \
                } \
            } break; \
        } \
    } while (0)

/**
 * @def SQLITE_WITH_VEC_ROW_1D
 * @brief Dispatches runtime count (1..8) to a stack-allocated SqliteValueVec<N> (or heap SqliteValueVec<> if > 8),
 *        providing a SqliteRowOwnedWrapper span with the specified identifier inside the block without requiring a wrapper class.
 */
#define SQLITE_WITH_VEC_ROW_1D(RowWrapperVar, runtime_count, ...) \
    do { \
        const int _sz = static_cast<int>(runtime_count); \
        switch (_sz) { \
            case 1: { SqliteValueVec<1> _arr(1); SqliteRowOwnedWrapper RowWrapperVar(_arr.data(), 1); __VA_ARGS__; } break; \
            case 2: { SqliteValueVec<2> _arr(2); SqliteRowOwnedWrapper RowWrapperVar(_arr.data(), 2); __VA_ARGS__; } break; \
            case 3: { SqliteValueVec<3> _arr(3); SqliteRowOwnedWrapper RowWrapperVar(_arr.data(), 3); __VA_ARGS__; } break; \
            case 4: { SqliteValueVec<4> _arr(4); SqliteRowOwnedWrapper RowWrapperVar(_arr.data(), 4); __VA_ARGS__; } break; \
            case 5: { SqliteValueVec<5> _arr(5); SqliteRowOwnedWrapper RowWrapperVar(_arr.data(), 5); __VA_ARGS__; } break; \
            case 6: { SqliteValueVec<6> _arr(6); SqliteRowOwnedWrapper RowWrapperVar(_arr.data(), 6); __VA_ARGS__; } break; \
            case 7: { SqliteValueVec<7> _arr(7); SqliteRowOwnedWrapper RowWrapperVar(_arr.data(), 7); __VA_ARGS__; } break; \
            case 8: { SqliteValueVec<8> _arr(8); SqliteRowOwnedWrapper RowWrapperVar(_arr.data(), 8); __VA_ARGS__; } break; \
            default: { \
                if (_sz <= 0) { \
                    SqliteRowOwnedWrapper RowWrapperVar(nullptr, 0); \
                    __VA_ARGS__; \
                } else { \
                    SqliteValueVec<> _arr(_sz); \
                    SqliteRowOwnedWrapper RowWrapperVar(_arr.data(), _sz); \
                    __VA_ARGS__; \
                } \
            } break; \
        } \
    } while (0)

/**
 * @def SQLITE_WITH_KEY_VAL_OWNED_8X8
 * @brief Dispatches runtime pk_count and val_count (1..8) to stack-allocated containers 
 *        (SqliteValueTuple for Key, SqliteValueVec for Value; or heap fallback if > 8),
 *        providing two SqliteRowOwnedWrapper spans (KeyWrapperVar, ValWrapperVar) inside the block.
 * 
 * Usage:
 *   SQLITE_WITH_KEY_VAL_OWNED_8X8(key_row, val_row, pk_cols, val_cols, {
 *       key_row[0] = 1001;
 *       val_row[0] = "payload";
 *       vtab_insert(key_row, val_row);
 *   });
 */
#define SQLITE_WITH_KEY_VAL_OWNED_8X8(KeyWrapperVar, ValWrapperVar, pk_count, val_count, ...) \
    SQLITE_WITH_ROW_OWNED_1D(KeyWrapperVar, pk_count, { \
        SQLITE_WITH_VEC_ROW_1D(ValWrapperVar, val_count, { \
            __VA_ARGS__; \
        }); \
    })

/**
 * @def SQLITE_WITH_TUPLE_KEY_VAL_OWNED_8X8
 * @brief Shorthand 8x8 scope macro allocating SqliteValueTuple for both Key and Value.
 */
#define SQLITE_WITH_TUPLE_KEY_VAL_OWNED_8X8(KeyWrapperVar, ValWrapperVar, pk_count, val_count, ...) \
    SQLITE_WITH_ROW_OWNED_1D(KeyWrapperVar, pk_count, { \
        SQLITE_WITH_ROW_OWNED_1D(ValWrapperVar, val_count, { \
            __VA_ARGS__; \
        }); \
    })

/**
 * @def SQLITE_WITH_VEC_KEY_VAL_OWNED_8X8
 * @brief Shorthand 8x8 scope macro allocating SqliteValueVec for both Key and Value.
 */
#define SQLITE_WITH_VEC_KEY_VAL_OWNED_8X8(KeyWrapperVar, ValWrapperVar, pk_count, val_count, ...) \
    SQLITE_WITH_VEC_ROW_1D(KeyWrapperVar, pk_count, { \
        SQLITE_WITH_VEC_ROW_1D(ValWrapperVar, val_count, { \
            __VA_ARGS__; \
        }); \
    })

// ============================================================================
// PART 8: Functional Row Scope Dispatchers (withSqliteRowOwned, withSqliteKeyValOwned)
// ============================================================================

/**
 * @brief Zero-heap stack allocation dispatcher using SqliteValueTuple for
 * sizes 1..8 (0 heap allocations). Falls back to SqliteValueTuple<> (N = 0) for
 * sizes > 8.
 *
 * Usage:
 * @code
 * withSqliteRowOwned(num_cols, [&](SqliteRowOwnedWrapper row) {
 *     row[0] = SqliteValueOwned(42);
 *     row[1] = SqliteValueOwned("sensor_alpha");
 *     insert_into_vtab(row);
 * });
 * @endcode
 *
 * @tparam Callable Lambda/Functor signature: `auto(SqliteRowOwnedWrapper row_wrapper)`
 * @param size Requested number of columns (1..8 for stack, >8 uses dynamic heap).
 * @param fn Visitor callback receiving the mutable wrapper span.
 * @return Return value of the user callback function.
 */
template <typename Callable>
inline auto withSqliteRowOwned(int size, Callable &&fn)
    -> decltype(fn(SqliteRowOwnedWrapper())) {
  switch (size) {
  case 1: {
    SqliteValueTuple<1> arr;
    return fn(SqliteRowOwnedWrapper(arr.data(), 1));
  }
  case 2: {
    SqliteValueTuple<2> arr;
    return fn(SqliteRowOwnedWrapper(arr.data(), 2));
  }
  case 3: {
    SqliteValueTuple<3> arr;
    return fn(SqliteRowOwnedWrapper(arr.data(), 3));
  }
  case 4: {
    SqliteValueTuple<4> arr;
    return fn(SqliteRowOwnedWrapper(arr.data(), 4));
  }
  case 5: {
    SqliteValueTuple<5> arr;
    return fn(SqliteRowOwnedWrapper(arr.data(), 5));
  }
  case 6: {
    SqliteValueTuple<6> arr;
    return fn(SqliteRowOwnedWrapper(arr.data(), 6));
  }
  case 7: {
    SqliteValueTuple<7> arr;
    return fn(SqliteRowOwnedWrapper(arr.data(), 7));
  }
  case 8: {
    SqliteValueTuple<8> arr;
    return fn(SqliteRowOwnedWrapper(arr.data(), 8));
  }
  default: {
    if (size <= 0) {
      return fn(SqliteRowOwnedWrapper(nullptr, 0));
    }
    // For sizes > 8, use SqliteValueTuple<> (N = 0) which compiles to the direct heap
    // tuple template specialization, allocating the dynamic buffer via sqlite3_malloc64.
    SqliteValueTuple<> arr(size);
    return fn(SqliteRowOwnedWrapper(arr.data(), size));
  }
  }
}

/**
 * @brief Zero-heap 2D scope dispatcher evaluating runtime key and value column counts (1..8),
 *        passing two mutable SqliteRowOwnedWrapper spans (Key & Value) to the callback.
 */
template <typename Callable>
inline auto withSqliteKeyValOwned(int pk_count, int val_count, Callable &&fn)
    -> decltype(fn(SqliteRowOwnedWrapper(), SqliteRowOwnedWrapper())) {
    return withSqliteRowOwned(pk_count, [&](SqliteRowOwnedWrapper key_row) {
        return withSqliteRowOwned(val_count, [&](SqliteRowOwnedWrapper val_row) {
            return fn(key_row, val_row);
        });
    });
}

#endif // SQLITE3_VALUE_CONTAINERS_HPP
