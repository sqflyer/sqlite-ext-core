#ifndef SQLITE3_ROW_HPP
#define SQLITE3_ROW_HPP

#include "sqlite3_allocator.hpp"
#include "sqlite3_value.hpp"
#include <sqlite3.h>

// Forward declaration of classes for tight integration
class SqliteStatement;
class SqliteRowView;
class SqliteRowOwnedView;
class SqliteRowOwnedWrapper;
template <size_t N, typename Enable> class SqliteValueTuple;
template <size_t N, typename Enable> class SqliteValueVec;

// ============================================================================
// Source Type Macros for SqliteRowView
// ============================================================================
#define SQLITE_ROW_SOURCE_STMT            0 /**< Backed by sqlite3_stmt* column values */
#define SQLITE_ROW_SOURCE_ARGV            1 /**< Backed by sqlite3_value** (UDF args / vtab) */
#define SQLITE_ROW_SOURCE_VIEW_ARRAY      2 /**< Backed by const SqliteValueView* array */
#define SQLITE_ROW_SOURCE_VIEW_PTR_ARRAY  3 /**< Backed by const SqliteValueView* const* (used for extracting non-contiguous column projections / PKs) */
#define SQLITE_ROW_SOURCE_EMPTY           4 /**< Empty row view (0 columns) */

typedef uint8_t SqliteRowSourceType;

// ============================================================================
// Source Type Macros for SqliteRowOwnedView
// ============================================================================
#define SQLITE_ROW_OWNED_SOURCE_ARRAY     0 /**< Backed by const SqliteValueOwned* array / span */
#define SQLITE_ROW_OWNED_SOURCE_PTR_ARRAY 1 /**< Backed by const SqliteValueOwned* const* non-contiguous pointer array */
#define SQLITE_ROW_OWNED_SOURCE_EMPTY     2 /**< Empty owned row view (0 columns) */

typedef uint8_t SqliteRowOwnedSourceType;

// ============================================================================
// ============================================================================
// Array, Hashing, Iterator & Standard Container Synthesis Macros
// ============================================================================

/**
 * @struct sqlite_random_access_iterator_tag
 * @brief Freestanding tag identifying random-access iterator category without
 * requiring <iterator>.
 */
struct sqlite_random_access_iterator_tag {};

/**
 * @class sqlite_reverse_iterator
 * @brief Standard-compliant bidirectional and random-access reverse iterator
 * adapter over pointer or iterator types.
 *
 * Provides standard reverse iteration traversal semantics (`rbegin()`,
 * `rend()`, `crbegin()`, `crend()`) across contiguous arrays, dynamic vectors,
 * and non-owning row views.
 *
 * @tparam Iter Underlying forward iterator or raw pointer type.
 */
template <typename Iter> class sqlite_reverse_iterator {
private:
  Iter m_current;

public:
  typedef sqlite_random_access_iterator_tag iterator_category;
  typedef Iter iterator_type;
  typedef decltype(*sqlite_declval<Iter>()) reference;
  typedef typename sqlite_remove_reference<reference>::type value_type;
  typedef value_type *pointer;
  typedef const value_type *const_pointer;
  typedef ptrdiff_t difference_type;

  /**
   * @class ArrowProxy
   * @brief Proxy object supporting operator-> on both lvalue references and
   * prvalue temporary views.
   */
  class ArrowProxy {
  private:
    value_type m_val;

  public:
    inline explicit ArrowProxy(const value_type &v) noexcept : m_val(v) {}
    inline const_pointer operator->() const noexcept { return &m_val; }
    inline pointer operator->() noexcept { return &m_val; }
  };

  /** @brief Default constructor initializing to null iterator. */
  inline sqlite_reverse_iterator() : m_current(nullptr) {}

  /** @brief Explicit constructor wrapping a base iterator. */
  inline explicit sqlite_reverse_iterator(Iter it) : m_current(it) {}

  /** @brief Converting copy constructor from compatible reverse iterator. */
  template <typename OtherIter>
  inline sqlite_reverse_iterator(
      const sqlite_reverse_iterator<OtherIter> &other)
      : m_current(other.base()) {}

  /** @brief Returns the underlying base iterator. */
  inline Iter base() const noexcept { return m_current; }

  /** @brief Dereferences the reverse iterator, yielding the element preceding
   * base(). */
  inline reference operator*() const noexcept {
    Iter tmp = m_current;
    return *--tmp;
  }

  /** @brief Member access operator via ArrowProxy. */
  inline ArrowProxy operator->() const noexcept {
    return ArrowProxy(operator*());
  }

  /** @brief Pre-increment: steps backward in base iterator sequence. */
  inline sqlite_reverse_iterator &operator++() noexcept {
    --m_current;
    return *this;
  }

  /** @brief Post-increment: steps backward in base iterator sequence. */
  inline sqlite_reverse_iterator operator++(int) noexcept {
    sqlite_reverse_iterator tmp = *this;
    --m_current;
    return tmp;
  }

  /** @brief Pre-decrement: steps forward in base iterator sequence. */
  inline sqlite_reverse_iterator &operator--() noexcept {
    ++m_current;
    return *this;
  }

  /** @brief Post-decrement: steps forward in base iterator sequence. */
  inline sqlite_reverse_iterator operator--(int) noexcept {
    sqlite_reverse_iterator tmp = *this;
    ++m_current;
    return tmp;
  }

  /** @brief Offset addition. */
  inline sqlite_reverse_iterator operator+(difference_type n) const noexcept {
    return sqlite_reverse_iterator(m_current - n);
  }

  /** @brief Offset compound addition. */
  inline sqlite_reverse_iterator &operator+=(difference_type n) noexcept {
    m_current -= n;
    return *this;
  }

  /** @brief Offset subtraction. */
  inline sqlite_reverse_iterator operator-(difference_type n) const noexcept {
    return sqlite_reverse_iterator(m_current + n);
  }

  /** @brief Offset compound subtraction. */
  inline sqlite_reverse_iterator &operator-=(difference_type n) noexcept {
    m_current += n;
    return *this;
  }

  /** @brief Iterator distance calculation. */
  inline difference_type
  operator-(const sqlite_reverse_iterator &o) const noexcept {
    return o.m_current - m_current;
  }

  /** @brief Subscript element access. */
  inline reference operator[](difference_type n) const noexcept {
    return *(*this + n);
  }

  /** @brief Relational equality comparison. */
  inline bool operator==(const sqlite_reverse_iterator &o) const noexcept {
    return m_current == o.m_current;
  }

  /** @brief Relational inequality comparison. */
  inline bool operator!=(const sqlite_reverse_iterator &o) const noexcept {
    return m_current != o.m_current;
  }

  /** @brief Relational less-than comparison. */
  inline bool operator<(const sqlite_reverse_iterator &o) const noexcept {
    return m_current > o.m_current;
  }

  /** @brief Relational less-than-or-equal comparison. */
  inline bool operator<=(const sqlite_reverse_iterator &o) const noexcept {
    return m_current >= o.m_current;
  }

  /** @brief Relational greater-than comparison. */
  inline bool operator>(const sqlite_reverse_iterator &o) const noexcept {
    return m_current < o.m_current;
  }

  /** @brief Relational greater-than-or-equal comparison. */
  inline bool operator>=(const sqlite_reverse_iterator &o) const noexcept {
    return m_current <= o.m_current;
  }
};

#ifndef SQLITE_DERIVE_ARRAY_HASH
/**
 * @brief Macro helper to synthesize uniform, zero-overhead MurmurHash2
 * composite hashing across all array/tabular containers (SqliteValueTuple,
 * SqliteValueVec, SqliteRowView, SqliteRowOwnedWrapper).
 */
#define SQLITE_DERIVE_ARRAY_HASH                                               \
  inline unsigned long long hash() const noexcept {                            \
    int sz = this->size();                                                     \
    if (sz == 0)                                                               \
      return SqliteHashUtil::DEFAULT_SEED;                                     \
    if (sz == 1)                                                               \
      return (*this)[0].hash();                                                \
    unsigned long long h = SqliteHashUtil::DEFAULT_SEED;                       \
    for (int i = 0; i < sz; ++i) {                                             \
      h = SqliteHashUtil::combine(h, (*this)[i].hash());                       \
    }                                                                          \
    return h;                                                                  \
  }
#endif

#ifndef SQLITE_DERIVE_ARRAY_ACCESSORS
/**
 * @brief Macro helper to synthesize uniform, zero-overhead indexed typed
 * extraction accessors (as_int64, as_int, as_double, as_text, as_blob, as_bool,
 * is_null, type, subtype) across all array/tabular containers.
 */
#define SQLITE_DERIVE_ARRAY_ACCESSORS                                          \
  inline sqlite3_int64 as_int64(int index = 0) const noexcept {                \
    return (*this)[index].as_int64();                                          \
  }                                                                            \
  inline int as_int(int index = 0) const noexcept {                            \
    return (*this)[index].as_int();                                            \
  }                                                                            \
  inline double as_double(int index = 0) const noexcept {                      \
    return (*this)[index].as_double();                                         \
  }                                                                            \
  inline SqliteStringView as_text(int index = 0) const noexcept {              \
    return (*this)[index].as_text();                                           \
  }                                                                            \
  inline SqliteBlobView as_blob(int index = 0) const noexcept {                \
    return (*this)[index].as_blob();                                           \
  }                                                                            \
  inline bool as_bool(int index = 0) const noexcept {                          \
    return (*this)[index].as_bool();                                           \
  }                                                                            \
  inline bool is_null(int index = 0) const noexcept {                          \
    return (*this)[index].is_null();                                           \
  }                                                                            \
  inline int type(int index = 0) const noexcept {                              \
    return (*this)[index].type();                                              \
  }                                                                            \
  inline uint8_t subtype(int index = 0) const noexcept {                       \
    return (*this)[index].subtype();                                           \
  }
#endif

#ifndef SQLITE_DERIVE_ARRAY_ITERATOR
/**
 * @brief Macro helper to synthesize standard C++11 bidirectional/random-access
 * iterators and begin()/end() enabling range-based for loops, reverse
 * iteration, and STL algorithms across dynamic row views.
 */
#define SQLITE_DERIVE_ARRAY_ITERATOR(ContainerType, ElementType)               \
  class Iterator {                                                             \
  private:                                                                     \
    const ContainerType *m_array;                                              \
    int m_idx;                                                                 \
                                                                               \
  public:                                                                      \
    typedef sqlite_random_access_iterator_tag iterator_category;               \
    typedef ElementType value_type;                                            \
    typedef ptrdiff_t difference_type;                                         \
    typedef const ElementType *pointer;                                        \
    typedef ElementType reference;                                             \
    inline Iterator() noexcept : m_array(nullptr), m_idx(0) {}                 \
    inline Iterator(const ContainerType *arr, int idx) noexcept                \
        : m_array(arr), m_idx(idx) {}                                          \
    inline ElementType operator*() const noexcept {                            \
      return (*m_array)[m_idx];                                                \
    }                                                                          \
    inline Iterator &operator++() noexcept {                                   \
      ++m_idx;                                                                 \
      return *this;                                                            \
    }                                                                          \
    inline Iterator operator++(int) noexcept {                                 \
      Iterator tmp = *this;                                                    \
      ++m_idx;                                                                 \
      return tmp;                                                              \
    }                                                                          \
    inline Iterator &operator--() noexcept {                                   \
      --m_idx;                                                                 \
      return *this;                                                            \
    }                                                                          \
    inline Iterator operator--(int) noexcept {                                 \
      Iterator tmp = *this;                                                    \
      --m_idx;                                                                 \
      return tmp;                                                              \
    }                                                                          \
    inline Iterator operator+(difference_type n) const noexcept {              \
      return Iterator(m_array, m_idx + static_cast<int>(n));                   \
    }                                                                          \
    inline Iterator &operator+=(difference_type n) noexcept {                  \
      m_idx += static_cast<int>(n);                                            \
      return *this;                                                            \
    }                                                                          \
    inline Iterator operator-(difference_type n) const noexcept {              \
      return Iterator(m_array, m_idx - static_cast<int>(n));                   \
    }                                                                          \
    inline Iterator &operator-=(difference_type n) noexcept {                  \
      m_idx -= static_cast<int>(n);                                            \
      return *this;                                                            \
    }                                                                          \
    inline difference_type operator-(const Iterator &o) const noexcept {       \
      return m_idx - o.m_idx;                                                  \
    }                                                                          \
    inline ElementType operator[](difference_type n) const noexcept {          \
      return (*m_array)[m_idx + static_cast<int>(n)];                          \
    }                                                                          \
    inline bool operator==(const Iterator &o) const noexcept {                 \
      return m_idx == o.m_idx && m_array == o.m_array;                         \
    }                                                                          \
    inline bool operator!=(const Iterator &o) const noexcept {                 \
      return !(*this == o);                                                    \
    }                                                                          \
    inline bool operator<(const Iterator &o) const noexcept {                  \
      return m_idx < o.m_idx;                                                  \
    }                                                                          \
    inline bool operator<=(const Iterator &o) const noexcept {                 \
      return m_idx <= o.m_idx;                                                 \
    }                                                                          \
    inline bool operator>(const Iterator &o) const noexcept {                  \
      return m_idx > o.m_idx;                                                  \
    }                                                                          \
    inline bool operator>=(const Iterator &o) const noexcept {                 \
      return m_idx >= o.m_idx;                                                 \
    }                                                                          \
  };                                                                           \
  typedef Iterator const_iterator;                                             \
  typedef Iterator iterator;                                                   \
  typedef sqlite_reverse_iterator<const_iterator> const_reverse_iterator;      \
  typedef sqlite_reverse_iterator<iterator> reverse_iterator;                  \
  inline const_iterator begin() const noexcept {                               \
    return const_iterator(this, 0);                                            \
  }                                                                            \
  inline const_iterator end() const noexcept {                                 \
    return const_iterator(this, this->size());                                 \
  }                                                                            \
  inline const_iterator cbegin() const noexcept {                              \
    return const_iterator(this, 0);                                            \
  }                                                                            \
  inline const_iterator cend() const noexcept {                                \
    return const_iterator(this, this->size());                                 \
  }                                                                            \
  inline const_reverse_iterator rbegin() const noexcept {                      \
    return const_reverse_iterator(end());                                      \
  }                                                                            \
  inline const_reverse_iterator rend() const noexcept {                        \
    return const_reverse_iterator(begin());                                    \
  }                                                                            \
  inline const_reverse_iterator crbegin() const noexcept {                     \
    return const_reverse_iterator(cend());                                     \
  }                                                                            \
  inline const_reverse_iterator crend() const noexcept {                       \
    return const_reverse_iterator(cbegin());                                   \
  }
#endif

#ifndef SQLITE_DERIVE_STANDARD_CONTAINER_TYPEDEFS
/**
 * @def SQLITE_DERIVE_STANDARD_CONTAINER_TYPEDEFS
 * @brief Synthesizes standard C++ container member types (value_type,
 * size_type, iterators, etc.).
 */
#define SQLITE_DERIVE_STANDARD_CONTAINER_TYPEDEFS(                             \
    ValType, RefType, ConstRefType, PtrType, ConstPtrType, IterType,           \
    ConstIterType)                                                             \
  typedef ValType value_type;                                                  \
  typedef size_t size_type;                                                    \
  typedef ptrdiff_t difference_type;                                           \
  typedef RefType reference;                                                   \
  typedef ConstRefType const_reference;                                        \
  typedef PtrType pointer;                                                     \
  typedef ConstPtrType const_pointer;                                          \
  typedef IterType iterator;                                                   \
  typedef ConstIterType const_iterator;                                        \
  typedef sqlite_reverse_iterator<iterator> reverse_iterator;                  \
  typedef sqlite_reverse_iterator<const_iterator> const_reverse_iterator;
#endif

#ifndef SQLITE_DERIVE_ARRAY_ITERATORS
/**
 * @def SQLITE_DERIVE_ARRAY_ITERATORS
 * @brief Synthesizes standard forward and reverse iterator accessors (begin,
 * end, cbegin, cend, rbegin, rend, crbegin, crend).
 * @param DataPtr Contiguous pointer to beginning of elements.
 * @param SizeVal Number of elements currently stored.
 */
#define SQLITE_DERIVE_ARRAY_ITERATORS(DataPtr, SizeVal)                        \
  inline iterator begin() noexcept { return (DataPtr); }                       \
  inline const_iterator begin() const noexcept { return (DataPtr); }           \
  inline const_iterator cbegin() const noexcept { return (DataPtr); }          \
  inline iterator end() noexcept { return (DataPtr) + (SizeVal); }             \
  inline const_iterator end() const noexcept { return (DataPtr) + (SizeVal); } \
  inline const_iterator cend() const noexcept {                                \
    return (DataPtr) + (SizeVal);                                              \
  }                                                                            \
  inline reverse_iterator rbegin() noexcept {                                  \
    return reverse_iterator(end());                                            \
  }                                                                            \
  inline const_reverse_iterator rbegin() const noexcept {                      \
    return const_reverse_iterator(end());                                      \
  }                                                                            \
  inline const_reverse_iterator crbegin() const noexcept {                     \
    return const_reverse_iterator(cend());                                     \
  }                                                                            \
  inline reverse_iterator rend() noexcept {                                    \
    return reverse_iterator(begin());                                          \
  }                                                                            \
  inline const_reverse_iterator rend() const noexcept {                        \
    return const_reverse_iterator(begin());                                    \
  }                                                                            \
  inline const_reverse_iterator crend() const noexcept {                       \
    return const_reverse_iterator(cbegin());                                   \
  }
#endif

#ifndef SQLITE_DERIVE_ARRAY_ELEMENT_ACCESSORS
/**
 * @def SQLITE_DERIVE_ARRAY_ELEMENT_ACCESSORS
 * @brief Synthesizes standard element accessors (front, back, at, operator[])
 * with boundary-checked fallback.
 * @param DataPtr Contiguous pointer to beginning of elements.
 * @param SizeVal Number of elements currently stored.
 * @param FallbackNull Reference to canonical null instance used on
 * out-of-bounds access.
 */
#define SQLITE_DERIVE_ARRAY_ELEMENT_ACCESSORS(DataPtr, SizeVal, FallbackNull)  \
  inline reference front() noexcept {                                          \
    return (SizeVal) > 0 ? (DataPtr)[0] : (FallbackNull);                      \
  }                                                                            \
  inline const_reference front() const noexcept {                              \
    return (SizeVal) > 0 ? (DataPtr)[0] : (FallbackNull);                      \
  }                                                                            \
  inline reference back() noexcept {                                           \
    return (SizeVal) > 0 ? (DataPtr)[(SizeVal) - 1] : (FallbackNull);          \
  }                                                                            \
  inline const_reference back() const noexcept {                               \
    return (SizeVal) > 0 ? (DataPtr)[(SizeVal) - 1] : (FallbackNull);          \
  }                                                                            \
  inline reference at(size_type pos) noexcept {                                \
    return (pos < static_cast<size_type>(SizeVal)) ? (DataPtr)[pos]            \
                                                   : (FallbackNull);           \
  }                                                                            \
  inline const_reference at(size_type pos) const noexcept {                    \
    return (pos < static_cast<size_type>(SizeVal)) ? (DataPtr)[pos]            \
                                                   : (FallbackNull);           \
  }                                                                            \
  inline reference operator[](int idx) noexcept {                              \
    return (idx >= 0 && idx < static_cast<int>(SizeVal)) ? (DataPtr)[idx]      \
                                                         : (FallbackNull);     \
  }                                                                            \
  inline const_reference operator[](int idx) const noexcept {                  \
    return (idx >= 0 && idx < static_cast<int>(SizeVal)) ? (DataPtr)[idx]      \
                                                         : (FallbackNull);     \
  }                                                                            \
  inline reference operator[](size_type idx) noexcept {                        \
    return (idx < static_cast<size_type>(SizeVal)) ? (DataPtr)[idx]            \
                                                   : (FallbackNull);           \
  }                                                                            \
  inline const_reference operator[](size_type idx) const noexcept {            \
    return (idx < static_cast<size_type>(SizeVal)) ? (DataPtr)[idx]            \
                                                   : (FallbackNull);           \
  }
#endif

#ifndef SQLITE_DERIVE_STD_ARRAY_METHODS
/**
 * @def SQLITE_DERIVE_STD_ARRAY_METHODS
 * @brief Synthesizes complete std::array compliant interface including
 * iterators, element accessors, and max_size().
 * @param DataPtr Contiguous pointer to beginning of elements.
 * @param SizeVal Number of elements currently stored.
 * @param FallbackNull Reference to canonical null instance.
 * @param MaxSizeVal Fixed maximum capacity value.
 */
#define SQLITE_DERIVE_STD_ARRAY_METHODS(DataPtr, SizeVal, FallbackNull,        \
                                        MaxSizeVal)                            \
  SQLITE_DERIVE_ARRAY_ITERATORS(DataPtr, SizeVal)                              \
  SQLITE_DERIVE_ARRAY_ELEMENT_ACCESSORS(DataPtr, SizeVal, FallbackNull)        \
  inline constexpr size_type max_size() const noexcept { return (MaxSizeVal); }
#endif

// ============================================================================
// Macro Helpers for Complete Relational Operators
// ============================================================================

#ifndef SQLITE_DERIVE_RELATIONAL_OPS
#define SQLITE_DERIVE_RELATIONAL_OPS(OtherType)                                \
  inline bool operator!=(const OtherType &other) const noexcept {              \
    return !(*this == other);                                                  \
  }                                                                            \
  inline bool operator<=(const OtherType &other) const noexcept {              \
    return !(other < *this);                                                   \
  }                                                                            \
  inline bool operator>(const OtherType &other) const noexcept {               \
    return other < *this;                                                      \
  }                                                                            \
  inline bool operator>=(const OtherType &other) const noexcept {              \
    return !(*this < other);                                                   \
  }
#endif

#ifndef SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS
#define SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(ContainerType)                  \
  inline bool operator==(const ContainerType &other) const noexcept {          \
    if (this->size() != other.size())                                          \
      return false;                                                            \
    int sz = this->size();                                                     \
    for (int i = 0; i < sz; ++i) {                                             \
      if (!((*this)[i] == other[i]))                                           \
        return false;                                                          \
    }                                                                          \
    return true;                                                               \
  }                                                                            \
  inline bool operator!=(const ContainerType &other) const noexcept {          \
    return !(*this == other);                                                  \
  }                                                                            \
  inline bool operator<(const ContainerType &other) const noexcept {           \
    int sz1 = this->size();                                                    \
    int sz2 = other.size();                                                    \
    int min_sz = sz1 < sz2 ? sz1 : sz2;                                        \
    for (int i = 0; i < min_sz; ++i) {                                         \
      if ((*this)[i] < other[i])                                               \
        return true;                                                           \
      if (other[i] < (*this)[i])                                               \
        return false;                                                          \
    }                                                                          \
    return sz1 < sz2;                                                          \
  }                                                                            \
  inline bool operator>(const ContainerType &other) const noexcept {           \
    int sz1 = this->size();                                                    \
    int sz2 = other.size();                                                    \
    int min_sz = sz1 < sz2 ? sz1 : sz2;                                        \
    for (int i = 0; i < min_sz; ++i) {                                         \
      if (other[i] < (*this)[i])                                               \
        return true;                                                           \
      if ((*this)[i] < other[i])                                               \
        return false;                                                          \
    }                                                                          \
    return sz1 > sz2;                                                          \
  }                                                                            \
  inline bool operator<=(const ContainerType &other) const noexcept {          \
    return !(*this > other);                                                   \
  }                                                                            \
  inline bool operator>=(const ContainerType &other) const noexcept {          \
    return !(*this < other);                                                   \
  }
#endif

#ifndef SQLITE_DERIVE_SCALAR_RELATIONAL_OPS
#define SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(ScalarType)                        \
  inline bool operator==(const ScalarType &val) const noexcept {               \
    return (this->size() == 1) ? ((*this)[0] == val) : false;                  \
  }                                                                            \
  inline bool operator!=(const ScalarType &val) const noexcept {               \
    return !(*this == val);                                                    \
  }                                                                            \
  inline bool operator<(const ScalarType &val) const noexcept {                \
    int sz = this->size();                                                     \
    if (sz == 0)                                                               \
      return true;                                                             \
    if ((*this)[0] < val)                                                      \
      return true;                                                             \
    if (val < (*this)[0])                                                      \
      return false;                                                            \
    return sz < 1;                                                             \
  }                                                                            \
  inline bool operator>(const ScalarType &val) const noexcept {                \
    int sz = this->size();                                                     \
    if (sz == 0)                                                               \
      return false;                                                            \
    if (val < (*this)[0])                                                      \
      return true;                                                             \
    if ((*this)[0] < val)                                                      \
      return false;                                                            \
    return sz > 1;                                                             \
  }                                                                            \
  inline bool operator<=(const ScalarType &val) const noexcept {               \
    return !(*this > val);                                                     \
  }                                                                            \
  inline bool operator>=(const ScalarType &val) const noexcept {               \
    return !(*this < val);                                                     \
  }
#endif

/**
 * @brief Macro helper synthesizing direct relational operators against raw
 * C-strings (`const char*`).
 *
 * Explicitly constructs a lightweight non-allocating `SqliteStringView(val)` to
 * prevent compiler overload resolution ambiguity between `SqliteStringView` and
 * `SqliteString` (from `sqlite3_buffer.hpp`).
 */
#ifndef SQLITE_DERIVE_CSTR_RELATIONAL_OPS
#define SQLITE_DERIVE_CSTR_RELATIONAL_OPS                                      \
  inline bool operator==(const char *val) const noexcept {                     \
    return (this->size() == 1) ? ((*this)[0] == SqliteStringView(val))         \
                               : false;                                        \
  }                                                                            \
  inline bool operator!=(const char *val) const noexcept {                     \
    return !(*this == val);                                                    \
  }                                                                            \
  inline bool operator<(const char *val) const noexcept {                      \
    int sz = this->size();                                                     \
    if (sz == 0)                                                               \
      return true;                                                             \
    SqliteStringView sv(val);                                                  \
    if ((*this)[0] < sv)                                                       \
      return true;                                                             \
    if (sv < (*this)[0])                                                       \
      return false;                                                            \
    return sz < 1;                                                             \
  }                                                                            \
  inline bool operator>(const char *val) const noexcept {                      \
    int sz = this->size();                                                     \
    if (sz == 0)                                                               \
      return false;                                                            \
    SqliteStringView sv(val);                                                  \
    if (sv < (*this)[0])                                                       \
      return true;                                                             \
    if ((*this)[0] < sv)                                                       \
      return false;                                                            \
    return sz > 1;                                                             \
  }                                                                            \
  inline bool operator<=(const char *val) const noexcept {                     \
    return !(*this > val);                                                     \
  }                                                                            \
  inline bool operator>=(const char *val) const noexcept {                     \
    return !(*this < val);                                                     \
  }
#endif

#ifndef SQLITE_DERIVE_ALL_SCALAR_RELATIONAL_OPS
#define SQLITE_DERIVE_ALL_SCALAR_RELATIONAL_OPS                                \
  SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(SqliteValueOwned)                        \
  SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(SqliteValueView)                         \
  SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(SqliteStringView)                        \
  SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(SqliteStringOwned)                       \
  SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(SqliteBlobView)                          \
  SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(SqliteBlobOwned)                         \
  SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(sqlite3_int64)                           \
  SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(long)                                    \
  SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(int)                                     \
  SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(unsigned int)                            \
  SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(unsigned long)                           \
  SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(unsigned long long)                      \
  SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(double)                                  \
  SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(bool)                                    \
  SQLITE_DERIVE_CSTR_RELATIONAL_OPS
#endif

#ifndef SQLITE_DERIVE_REVERSE_RELATIONAL_OPS
#define SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(LhsType, RhsType)                 \
  inline bool operator==(const LhsType &lhs, const RhsType &rhs) noexcept {    \
    return rhs == lhs;                                                         \
  }                                                                            \
  inline bool operator!=(const LhsType &lhs, const RhsType &rhs) noexcept {    \
    return !(rhs == lhs);                                                      \
  }                                                                            \
  inline bool operator<(const LhsType &lhs, const RhsType &rhs) noexcept {     \
    return rhs > lhs;                                                          \
  }                                                                            \
  inline bool operator<=(const LhsType &lhs, const RhsType &rhs) noexcept {    \
    return rhs >= lhs;                                                         \
  }                                                                            \
  inline bool operator>(const LhsType &lhs, const RhsType &rhs) noexcept {     \
    return rhs < lhs;                                                          \
  }                                                                            \
  inline bool operator>=(const LhsType &lhs, const RhsType &rhs) noexcept {    \
    return rhs <= lhs;                                                         \
  }
#endif

#ifndef SQLITE_DERIVE_TEMPLATE_REVERSE_RELATIONAL_OPS
#define SQLITE_DERIVE_TEMPLATE_REVERSE_RELATIONAL_OPS(LhsType, RhsTemplate)    \
  template <size_t N, typename Enable>                                         \
  inline bool operator==(const LhsType &lhs,                                   \
                         const RhsTemplate<N, Enable> &rhs) noexcept {         \
    return rhs == lhs;                                                         \
  }                                                                            \
  template <size_t N, typename Enable>                                         \
  inline bool operator!=(const LhsType &lhs,                                   \
                         const RhsTemplate<N, Enable> &rhs) noexcept {         \
    return !(rhs == lhs);                                                      \
  }                                                                            \
  template <size_t N, typename Enable>                                         \
  inline bool operator<(const LhsType &lhs,                                    \
                        const RhsTemplate<N, Enable> &rhs) noexcept {          \
    return rhs > lhs;                                                          \
  }                                                                            \
  template <size_t N, typename Enable>                                         \
  inline bool operator<=(const LhsType &lhs,                                   \
                         const RhsTemplate<N, Enable> &rhs) noexcept {         \
    return rhs >= lhs;                                                         \
  }                                                                            \
  template <size_t N, typename Enable>                                         \
  inline bool operator>(const LhsType &lhs,                                    \
                        const RhsTemplate<N, Enable> &rhs) noexcept {          \
    return rhs < lhs;                                                          \
  }                                                                            \
  template <size_t N, typename Enable>                                         \
  inline bool operator>=(const LhsType &lhs,                                   \
                         const RhsTemplate<N, Enable> &rhs) noexcept {         \
    return rhs <= lhs;                                                         \
  }
#endif

#ifndef SQLITE_DERIVE_ALL_REVERSE_RELATIONAL_OPS
#define SQLITE_DERIVE_ALL_REVERSE_RELATIONAL_OPS(TargetClass)                  \
  SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteValueOwned, TargetClass)          \
  SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteValueView, TargetClass)           \
  SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteStringView, TargetClass)          \
  SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteStringOwned, TargetClass)         \
  SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteBlobView, TargetClass)            \
  SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteBlobOwned, TargetClass)           \
  SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(sqlite3_int64, TargetClass)             \
  SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(long, TargetClass)                      \
  SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(int, TargetClass)                       \
  SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(unsigned int, TargetClass)              \
  SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(unsigned long, TargetClass)             \
  SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(unsigned long long, TargetClass)        \
  SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(double, TargetClass)                    \
  SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(bool, TargetClass)                      \
  inline bool operator==(const char *lhs, const TargetClass &rhs) noexcept {   \
    return rhs == lhs;                                                         \
  }                                                                            \
  inline bool operator!=(const char *lhs, const TargetClass &rhs) noexcept {   \
    return rhs != lhs;                                                         \
  }                                                                            \
  inline bool operator<(const char *lhs, const TargetClass &rhs) noexcept {    \
    return rhs > lhs;                                                          \
  }                                                                            \
  inline bool operator<=(const char *lhs, const TargetClass &rhs) noexcept {   \
    return rhs >= lhs;                                                         \
  }                                                                            \
  inline bool operator>(const char *lhs, const TargetClass &rhs) noexcept {    \
    return rhs < lhs;                                                          \
  }                                                                            \
  inline bool operator>=(const char *lhs, const TargetClass &rhs) noexcept {   \
    return rhs <= lhs;                                                         \
  }
#endif

// Forward declarations of row view and wrapper classes
class SqliteRowView;
class SqliteRowOwnedView;
class SqliteRowOwnedWrapper;
template <size_t N, typename Enable> class SqliteValueTuple;
template <size_t N, typename Enable> class SqliteValueVec;

// ============================================================================
// 1. SqliteRowView: Universal Non-Owning Multi-Column Row View
// ============================================================================

/**
 * @brief Lightweight, zero-allocation non-owning view over a multi-column
 * SQLite row or UDF arguments.
 *
 * `SqliteRowView` provides uniform, zero-copy, bounds-checked access to
 * multi-column tabular rows and UDF argument vectors. It acts as a universal
 * abstraction layer over:
 * - Active prepared statement rows (`sqlite3_stmt*` or `SqliteStatement&`)
 * - SQLite UDF / aggregate / vtab argument vectors (`sqlite3_value**` with
 * `argc`)
 * - In-memory contiguous view arrays (`const SqliteValueView*`)
 *
 * All column access operations (`operator[]`, `as_text()`, `as_int64()`, etc.)
 * return lightweight non-owning views with zero heap allocations.
 */
class SqliteRowView {
private:
  union {
    SqliteStatement *m_stmt;
    sqlite3_value **m_argv;
    const SqliteValueView *m_view_array;
    const SqliteValueView *const *m_view_ptr_array;
  };
  int m_col_count;
  SqliteRowSourceType m_source; // SQLITE_ROW_SOURCE_*

public:
  /**
   * @brief Constructs an empty row view with zero columns.
   */
  inline SqliteRowView() noexcept
      : m_stmt(nullptr), m_col_count(0), m_source(SQLITE_ROW_SOURCE_EMPTY) {}

  /**
   * @brief Constructs a row view wrapping an active prepared statement step
   * row.
   *
   * @param stmt The raw SQLite statement handle (sqlite3_stmt*) positioned at
   * SQLITE_ROW.
   */
  inline explicit SqliteRowView(sqlite3_stmt *stmt) noexcept
      : m_stmt(reinterpret_cast<SqliteStatement *>(stmt)),
        m_col_count(stmt ? sqlite3_column_count(stmt) : 0),
        m_source(SQLITE_ROW_SOURCE_STMT) {}

  /**
   * @brief Constructs a row view wrapping an active SqliteStatement pointer
   * positioned at SQLITE_ROW.
   *
   * @param stmt Pointer to SqliteStatement wrapper.
   */
  inline explicit SqliteRowView(SqliteStatement *stmt) noexcept
      : m_stmt(stmt),
        m_col_count(
            stmt ? sqlite3_column_count(reinterpret_cast<sqlite3_stmt *>(stmt))
                 : 0),
        m_source(SQLITE_ROW_SOURCE_STMT) {}

  /**
   * @brief Constructs a row view wrapping raw UDF / vtab argument arrays.
   *
   * @param argc Number of arguments in the array.
   * @param argv Array of sqlite3_value* pointers.
   */
  inline SqliteRowView(int argc, sqlite3_value **argv) noexcept
      : m_argv(argv), m_col_count(argc >= 0 ? argc : 0),
        m_source(SQLITE_ROW_SOURCE_ARGV) {}

  /**
   * @brief Constructs a row view wrapping a raw argument vector with an
   * optional count.
   */
  inline explicit SqliteRowView(sqlite3_value **argv, int argc = 0) noexcept
      : m_argv(argv), m_col_count(argc >= 0 ? argc : 0),
        m_source(SQLITE_ROW_SOURCE_ARGV) {}

  /**
   * @brief Constructs a row view wrapping a contiguous array of
   * SqliteValueView.
   *
   * @param array Pointer to the first SqliteValueView element.
   * @param count Number of column views in the array.
   */
  inline SqliteRowView(const SqliteValueView *array, int count) noexcept
      : m_view_array(array), m_col_count(count >= 0 ? count : 0),
        m_source(SQLITE_ROW_SOURCE_VIEW_ARRAY) {}

  /**
   * @brief Constructs a row view wrapping an array of pointers to single
   * SqliteValueView instances.
   *
   * Primarily used for extracting primary keys (or arbitrary column
   * projections) from a complete row, as primary key columns may not be stored
   * contiguously in memory within the complete row layout. This avoids
   * allocating or copying intermediate column buffers.
   *
   * @param array Array of pointers to single SqliteValueView objects.
   * @param count Number of column view pointers in the array.
   */
  inline SqliteRowView(const SqliteValueView *const *array, int count) noexcept
      : m_view_ptr_array(array), m_col_count(count >= 0 ? count : 0),
        m_source(SQLITE_ROW_SOURCE_VIEW_PTR_ARRAY) {}

  inline SqliteRowView(const SqliteValueView **array, int count) noexcept
      : m_view_ptr_array(const_cast<const SqliteValueView *const *>(array)),
        m_col_count(count >= 0 ? count : 0),
        m_source(SQLITE_ROW_SOURCE_VIEW_PTR_ARRAY) {}

  // Default copy/move semantics (Trivial 16-byte register copy)
  SqliteRowView(const SqliteRowView &) noexcept = default;
  SqliteRowView &operator=(const SqliteRowView &) noexcept = default;
  SqliteRowView(SqliteRowView &&) noexcept = default;
  SqliteRowView &operator=(SqliteRowView &&) noexcept = default;

  // Standard Container Types
  typedef SqliteValueView value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef SqliteValueView reference;
  typedef SqliteValueView const_reference;
  typedef const SqliteValueView *pointer;
  typedef const SqliteValueView *const_pointer;

  /** @brief Returns the total number of columns in the row view. */
  inline int size() const noexcept { return m_col_count; }

  /** @brief Alias for size() returning the column count. */
  inline int count() const noexcept { return m_col_count; }

  /** @brief Alias for size() returning the column count. */
  inline int column_count() const noexcept { return m_col_count; }

  /** @brief Alias for size() returning argument count (useful for UDFs). */
  inline int argc() const noexcept { return m_col_count; }

  /** @brief Checks if the row view contains zero columns. */
  inline bool empty() const noexcept { return m_col_count == 0; }

  /** @brief Maximum theoretical elements supported. */
  inline constexpr size_type max_size() const noexcept {
    return static_cast<size_type>(-1) / sizeof(SqliteValueView);
  }

  // =========================================================================
  // Element Accessors (Zero Allocations, Complete std::array Compliance)
  // =========================================================================

  /**
   * @brief Accesses the first column in the row.
   * @return SqliteValueView representing column 0 (or NULL view if empty).
   */
  inline SqliteValueView front() const noexcept { return (*this)[0]; }

  /**
   * @brief Accesses the last column in the row.
   * @return SqliteValueView representing the last column (or NULL view if
   * empty).
   */
  inline SqliteValueView back() const noexcept {
    return m_col_count > 0 ? (*this)[m_col_count - 1]
                           : SqliteValueView(nullptr);
  }

  /**
   * @brief Subscript operator with out-of-bounds safety.
   *
   * @param col 0-indexed column position.
   * @return SqliteValueView representing the column (or fallback NULL if out of
   * bounds).
   */
  inline SqliteValueView operator[](int col) const noexcept {
    if (col < 0 || col >= m_col_count) {
      return SqliteValueView(nullptr);
    }
    switch (m_source) {
    case SQLITE_ROW_SOURCE_STMT:
      return SqliteValueView::from_column(raw_stmt(), col);
    case SQLITE_ROW_SOURCE_ARGV:
      return (m_argv && m_argv[col]) ? SqliteValueView(m_argv[col])
                                     : SqliteValueView(nullptr);
    case SQLITE_ROW_SOURCE_VIEW_ARRAY:
      return m_view_array[col];
    case SQLITE_ROW_SOURCE_VIEW_PTR_ARRAY:
      return (m_view_ptr_array && m_view_ptr_array[col])
                 ? *m_view_ptr_array[col]
                 : SqliteValueView(nullptr);
    default:
      return SqliteValueView(nullptr);
    }
  }

  inline SqliteValueView operator[](size_type col) const noexcept {
    return (*this)[static_cast<int>(col)];
  }

  /** @brief Bounds-safe element accessor identical to operator[]. */
  inline SqliteValueView at(int col) const noexcept { return (*this)[col]; }
  inline SqliteValueView at(size_type col) const noexcept {
    return (*this)[static_cast<int>(col)];
  }

  /** @brief Column accessor alias identical to operator[]. */
  inline SqliteValueView get_column(int col) const noexcept {
    return (*this)[col];
  }
  inline SqliteValueView column(int col) const noexcept {
    return (*this)[col];
  }

  /**
   * @brief Returns the column name (only available when backed by a prepared
   * statement).
   *
   * @param col 0-indexed column index.
   * @return UTF-8 column name string, or nullptr if unavailable / out of
   * bounds.
   */
  inline const char *column_name(int col) const noexcept {
    if (m_source == SQLITE_ROW_SOURCE_STMT && m_stmt && col >= 0 &&
        col < m_col_count) {
      return sqlite3_column_name(raw_stmt(), col);
    }
    return nullptr;
  }

  /**
   * @brief Returns the declared column datatype in the table schema (e.g.
   * "INTEGER", "TEXT").
   *
   * Only available when backed by a prepared statement.
   *
   * @param col 0-indexed column index.
   * @return Declared column type string, or nullptr if unavailable / out of
   * bounds.
   */
  inline const char *column_decltype(int col) const noexcept {
    if (m_source == SQLITE_ROW_SOURCE_STMT && m_stmt && col >= 0 &&
        col < m_col_count) {
      return sqlite3_column_decltype(raw_stmt(), col);
    }
    return nullptr;
  }

  // Typed Column Extraction Accessors & Composite Hashing
  SQLITE_DERIVE_ARRAY_ACCESSORS
  SQLITE_DERIVE_ARRAY_HASH

  // =========================================================================
  // Full Relational Operators (==, !=, <, <=, >, >=)
  // =========================================================================
  SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteRowView)
  SQLITE_DERIVE_ALL_SCALAR_RELATIONAL_OPS

  /** @brief Returns internal source type (SQLITE_ROW_SOURCE_*). */
  inline SqliteRowSourceType source_type() const noexcept { return m_source; }
  inline sqlite3_stmt *raw_stmt() const noexcept {
    return reinterpret_cast<sqlite3_stmt *>(m_stmt);
  }
  inline SqliteStatement *statement() const noexcept { return m_stmt; }
  inline sqlite3_value **raw_argv() const noexcept { return m_argv; }
  inline const SqliteValueView *raw_view_array() const noexcept {
    return m_view_array;
  }
  inline const SqliteValueView *const *raw_view_ptr_array() const noexcept {
    return m_view_ptr_array;
  }

  // Range-Based For Loop Iterator
  SQLITE_DERIVE_ARRAY_ITERATOR(SqliteRowView, SqliteValueView)
};

static_assert(sizeof(SqliteRowView) == 16,
              "SqliteRowView must be exactly 16 bytes!");

#ifndef SQLITE3_UDF_ARGS_DEFINED
#define SQLITE3_UDF_ARGS_DEFINED
/**
 * @brief Type alias providing unified nomenclature for scalar UDF arguments.
 */
typedef SqliteRowView SqliteUdfArgs;
#endif

// Symmetric reverse operators for SqliteRowView
SQLITE_DERIVE_ALL_REVERSE_RELATIONAL_OPS(SqliteRowView)

// ============================================================================
// 2. SqliteRowOwnedView: 16-Byte Universal Non-Owning View over SqliteValueOwned
// ============================================================================

/**
 * @class SqliteRowOwnedView
 * @brief Zero-allocation 16-byte universal read-only view over contiguous or non-contiguous
 *        SqliteValueOwned memory buffers.
 *
 * Multiplexes:
 * - Contiguous arrays / spans (`const SqliteValueOwned* m_array`)
 * - Non-contiguous pointer arrays (`const SqliteValueOwned* const* m_ptr_array`)
 *   for extracting primary key columns or projected columns from complete tabular rows
 *   without memory copying or intermediate buffers.
 * - Single values (`const SqliteValueOwned&`)
 * - Tuple containers (`SqliteValueTuple<N>`)
 * - Dynamic vector containers (`SqliteValueVec<N>`)
 * - Row wrappers (`SqliteRowOwnedWrapper`)
 *
 * Size is exactly 16 bytes (2 CPU registers: rax, rdx).
 */
class SqliteRowOwnedView {
private:
  union {
    const SqliteValueOwned *m_array;
    const SqliteValueOwned *const *m_ptr_array;
  };
  int m_col_count;
  SqliteRowOwnedSourceType m_source; // SQLITE_ROW_OWNED_SOURCE_*

  static inline const SqliteValueOwned &fallback_null() noexcept {
    static const SqliteValueOwned null_val;
    return null_val;
  }

public:
  /** @brief Constructs an empty owned row view with zero columns. */
  inline SqliteRowOwnedView() noexcept
      : m_array(nullptr), m_col_count(0),
        m_source(SQLITE_ROW_OWNED_SOURCE_EMPTY) {}

  /** @brief Constructs a view over a contiguous array of SqliteValueOwned. */
  inline SqliteRowOwnedView(const SqliteValueOwned *array, int count) noexcept
      : m_array(array), m_col_count(array && count > 0 ? count : 0),
        m_source(array && count > 0 ? SQLITE_ROW_OWNED_SOURCE_ARRAY
                                    : SQLITE_ROW_OWNED_SOURCE_EMPTY) {}

  /** @brief Constructs a 1-column view over a single SqliteValueOwned. */
  inline explicit SqliteRowOwnedView(const SqliteValueOwned &val) noexcept
      : m_array(&val), m_col_count(1),
        m_source(SQLITE_ROW_OWNED_SOURCE_ARRAY) {}

  /**
   * @brief Constructs a view over an array of pointers to SqliteValueOwned instances.
   *
   * Used for extracting primary keys or non-contiguous column projections from a complete
   * row without allocating intermediate buffers or memory copies.
   */
  inline SqliteRowOwnedView(const SqliteValueOwned *const *ptr_array,
                            int count) noexcept
      : m_ptr_array(ptr_array),
        m_col_count(ptr_array && count > 0 ? count : 0),
        m_source(ptr_array && count > 0 ? SQLITE_ROW_OWNED_SOURCE_PTR_ARRAY
                                        : SQLITE_ROW_OWNED_SOURCE_EMPTY) {}

  inline SqliteRowOwnedView(const SqliteValueOwned **ptr_array,
                            int count) noexcept
      : m_ptr_array(const_cast<const SqliteValueOwned *const *>(ptr_array)),
        m_col_count(ptr_array && count > 0 ? count : 0),
        m_source(ptr_array && count > 0 ? SQLITE_ROW_OWNED_SOURCE_PTR_ARRAY
                                        : SQLITE_ROW_OWNED_SOURCE_EMPTY) {}

  /** @brief Constructs a view from a SqliteRowOwnedWrapper span. */
  inline explicit SqliteRowOwnedView(const SqliteRowOwnedWrapper &wrap) noexcept;

  /** @brief Constructs a view from a SqliteValueTuple<N>. */
  template <size_t N, typename Enable>
  inline explicit SqliteRowOwnedView(
      const SqliteValueTuple<N, Enable> &tuple) noexcept;

  /** @brief Constructs a view from a SqliteValueVec<N>. */
  template <size_t N, typename Enable>
  inline explicit SqliteRowOwnedView(
      const SqliteValueVec<N, Enable> &vec) noexcept;

  // Default copy/move semantics (Trivial 16-byte register copy)
  SqliteRowOwnedView(const SqliteRowOwnedView &) noexcept = default;
  SqliteRowOwnedView &
  operator=(const SqliteRowOwnedView &) noexcept = default;
  SqliteRowOwnedView(SqliteRowOwnedView &&) noexcept = default;
  SqliteRowOwnedView &operator=(SqliteRowOwnedView &&) noexcept = default;

  // Size / Count Accessors
  inline int size() const noexcept { return m_col_count; }
  inline int count() const noexcept { return m_col_count; }
  inline int column_count() const noexcept { return m_col_count; }
  inline int argc() const noexcept { return m_col_count; }
  inline bool empty() const noexcept { return m_col_count == 0; }
  inline constexpr size_t max_size() const noexcept {
    return static_cast<size_t>(-1) / sizeof(SqliteValueOwned);
  }

  // Standard Container Types
  typedef SqliteValueOwned value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef const SqliteValueOwned &reference;
  typedef const SqliteValueOwned &const_reference;
  typedef const SqliteValueOwned *pointer;
  typedef const SqliteValueOwned *const_pointer;

  // Element Accessors
  inline const SqliteValueOwned &front() const noexcept { return (*this)[0]; }
  inline const SqliteValueOwned &back() const noexcept {
    return m_col_count > 0 ? (*this)[m_col_count - 1] : fallback_null();
  }

  inline const SqliteValueOwned &operator[](int col) const noexcept {
    if (col < 0 || col >= m_col_count)
      return fallback_null();
    if (m_source == SQLITE_ROW_OWNED_SOURCE_PTR_ARRAY) {
      return (m_ptr_array && m_ptr_array[col]) ? *m_ptr_array[col]
                                               : fallback_null();
    }
    return m_array ? m_array[col] : fallback_null();
  }
  inline const SqliteValueOwned &operator[](size_type col) const noexcept {
    return (*this)[static_cast<int>(col)];
  }

  inline const SqliteValueOwned &at(int col) const noexcept {
    return (*this)[col];
  }
  inline const SqliteValueOwned &at(size_type col) const noexcept {
    return (*this)[static_cast<int>(col)];
  }
  inline const SqliteValueOwned &get_column(int col) const noexcept {
    return (*this)[col];
  }
  inline const SqliteValueOwned &column(int col) const noexcept {
    return (*this)[col];
  }

  // Typed Extraction Accessors & MurmurHash2 Calculation
  SQLITE_DERIVE_ARRAY_ACCESSORS
  SQLITE_DERIVE_ARRAY_HASH

  // Relational Operators
  SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteRowOwnedView)
  SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteRowView)
  SQLITE_DERIVE_ALL_SCALAR_RELATIONAL_OPS

  // Introspection Accessors
  inline SqliteRowOwnedSourceType source_type() const noexcept {
    return m_source;
  }
  inline bool is_ptr_array() const noexcept {
    return m_source == SQLITE_ROW_OWNED_SOURCE_PTR_ARRAY;
  }
  inline const SqliteValueOwned *raw_array() const noexcept {
    return (m_source == SQLITE_ROW_OWNED_SOURCE_ARRAY) ? m_array : nullptr;
  }
  inline const SqliteValueOwned *const *raw_ptr_array() const noexcept {
    return (m_source == SQLITE_ROW_OWNED_SOURCE_PTR_ARRAY) ? m_ptr_array
                                                           : nullptr;
  }

  // Range-Based For Loop Iterator
  SQLITE_DERIVE_ARRAY_ITERATOR(SqliteRowOwnedView, SqliteValueOwned)
};

static_assert(sizeof(SqliteRowOwnedView) == 16,
              "SqliteRowOwnedView must be exactly 16 bytes!");

// Symmetric reverse operators for SqliteRowOwnedView
SQLITE_DERIVE_ALL_REVERSE_RELATIONAL_OPS(SqliteRowOwnedView)
SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteRowView, SqliteRowOwnedView)

// ============================================================================
// 2.5. SqliteRowUtil: Shared Row Construction Utilities
// ============================================================================
namespace SqliteRowUtil {
/**
 * @brief Copies up to `count` columns from a SqliteRowView into a contiguous
 *        SqliteValueOwned destination buffer.
 *
 * @param dest  Pointer to the destination SqliteValueOwned buffer.
 * @param view  The non-owning source row view.
 * @param count Number of elements to copy.
 */
inline void copy_from_view(SqliteValueOwned *dest, const SqliteRowView &view,
                           int count) noexcept {
  for (int i = 0; i < count; ++i) {
    dest[i] = view[i].to_owned();
  }
}
} // namespace SqliteRowUtil

// ============================================================================
// 3. SqliteRowOwnedWrapper: 16-Byte Span over Contiguous SqliteValueOwned
// ============================================================================

/**
 * @class SqliteRowOwnedWrapper
 * @brief Zero-allocation 16-byte span (const SqliteValueOwned* + int len) over
 * contiguous owned value buffers.
 *
 * Fits in 2 CPU registers (rax, rdx). Wraps and compares against:
 * - Single scalar values (SqliteValueOwned, SqliteValueView)
 * - Strings (SqliteStringView, SqliteStringOwned)
 * - Transient argument vectors (SqliteUdfArgs / SqliteRowView)
 * - Tuple containers (SqliteValueTuple<N>)
 * - Dynamic vector containers (SqliteValueVec<N>)
 */
class SqliteRowOwnedWrapper {
private:
  SqliteValueOwned *m_data;
  int m_len;

  /**
   * @brief Freestanding static immutable sink that safely absorbs out-of-bounds writes.
   * Since the instance is marked immutable, all mutation operations are safe no-ops,
   * making a single process-wide instance completely thread-safe without thread-local overhead.
   */
  static inline SqliteValueOwned& mutable_fallback_null() noexcept {
    static SqliteValueOwned immutable_null(nullptr, true);
    return immutable_null;
  }

  static inline const SqliteValueOwned& fallback_null() noexcept {
    return mutable_fallback_null();
  }

public:
  // =========================================================================
  // Constructors (Zero Copies, Zero Dynamic Allocations)
  // =========================================================================

  /**
   * @brief Constructs an empty row wrapper with zero columns.
   */
  inline SqliteRowOwnedWrapper() noexcept : m_data(nullptr), m_len(0) {}

  /**
   * @brief Constructs a row wrapper spanning a contiguous array of
   * SqliteValueOwned.
   *
   * @param data Pointer to the first SqliteValueOwned element.
   * @param len  Number of columns in the array.
   */
  inline SqliteRowOwnedWrapper(const SqliteValueOwned *data, int len) noexcept
      : m_data(const_cast<SqliteValueOwned *>(data)),
        m_len(data && len > 0 ? len : 0) {}

  /**
   * @brief Constructs a 1-column row wrapper spanning a single
   * SqliteValueOwned.
   *
   * @param val Reference to the single value.
   */
  inline explicit SqliteRowOwnedWrapper(const SqliteValueOwned &val) noexcept
      : m_data(const_cast<SqliteValueOwned *>(&val)), m_len(1) {}

  /**
   * @brief Constructs a row wrapper spanning a SqliteValueTuple<N>.
   */
  template <size_t N, typename Enable>
  inline SqliteRowOwnedWrapper(
      const SqliteValueTuple<N, Enable> &tuple) noexcept
      : m_data(const_cast<SqliteValueOwned *>(tuple.data())),
        m_len(tuple.size()) {}

  /**
   * @brief Constructs a row wrapper spanning a SqliteValueVec<N>.
   */
  template <size_t N, typename Enable>
  inline SqliteRowOwnedWrapper(const SqliteValueVec<N, Enable> &vec) noexcept
      : m_data(const_cast<SqliteValueOwned *>(vec.data())), m_len(vec.size()) {}

  // Default copy/move semantics (Trivial 16-byte register copy)
  SqliteRowOwnedWrapper(const SqliteRowOwnedWrapper &) noexcept = default;
  SqliteRowOwnedWrapper &
  operator=(const SqliteRowOwnedWrapper &) noexcept = default;
  SqliteRowOwnedWrapper(SqliteRowOwnedWrapper &&) noexcept = default;
  SqliteRowOwnedWrapper &operator=(SqliteRowOwnedWrapper &&) noexcept = default;

  // =========================================================================
  // Static Factory Methods
  // =========================================================================

  /** @brief Factory method creating a wrapper span from a raw pointer and size.
   */
  static inline SqliteRowOwnedWrapper create(const SqliteValueOwned *data,
                                             int size) noexcept {
    return SqliteRowOwnedWrapper(data, size);
  }
  /** @brief Factory method creating a 1-column wrapper span from a single
   * value. */
  static inline SqliteRowOwnedWrapper
  create(const SqliteValueOwned &val) noexcept {
    return SqliteRowOwnedWrapper(&val, 1);
  }
  /** @brief Factory method creating a wrapper span from a tuple. */
  template <size_t N, typename Enable>
  static inline SqliteRowOwnedWrapper
  create(const SqliteValueTuple<N, Enable> &tuple) noexcept {
    return SqliteRowOwnedWrapper(tuple.data(), tuple.size());
  }
  /** @brief Factory method creating a wrapper span from a vector. */
  template <size_t N, typename Enable>
  static inline SqliteRowOwnedWrapper
  create(const SqliteValueVec<N, Enable> &vec) noexcept {
    return SqliteRowOwnedWrapper(vec.data(), vec.size());
  }

  // Standard C++ Container Type Definitions
  SQLITE_DERIVE_STANDARD_CONTAINER_TYPEDEFS(
      SqliteValueOwned, SqliteValueOwned &, const SqliteValueOwned &,
      SqliteValueOwned *, const SqliteValueOwned *, SqliteValueOwned *,
      const SqliteValueOwned *)

  // =========================================================================
  // Capacity & Element Accessors
  // =========================================================================

  /** @brief Returns the total number of columns spanned by this wrapper. */
  inline int size() const noexcept { return m_len; }

  /** @brief Alias for size() returning the total column count. */
  inline int count() const noexcept { return m_len; }

  /** @brief Alias for size() returning the column count. */
  inline int column_count() const noexcept { return m_len; }

  /** @brief Checks if the wrapper spans zero columns or has a null data
   * pointer. */
  inline bool empty() const noexcept { return m_len == 0 || m_data == nullptr; }

  /** @brief Maximum theoretical elements supported. */
  inline constexpr size_type max_size() const noexcept {
    return static_cast<size_type>(-1) / sizeof(SqliteValueOwned);
  }

  /** @brief Returns a mutable pointer to the underlying column array. */
  inline pointer data() noexcept { return m_data; }

  /** @brief Returns a read-only pointer to the underlying column array. */
  inline const_pointer data() const noexcept { return m_data; }

  /** @brief Access first element. */
  inline reference front() noexcept {
    return (m_data && m_len > 0) ? m_data[0] : mutable_fallback_null();
  }
  inline const_reference front() const noexcept {
    return (m_data && m_len > 0) ? m_data[0] : fallback_null();
  }

  /** @brief Access last element. */
  inline reference back() noexcept {
    return (m_data && m_len > 0) ? m_data[m_len - 1] : mutable_fallback_null();
  }
  inline const_reference back() const noexcept {
    return (m_data && m_len > 0) ? m_data[m_len - 1] : fallback_null();
  }

  /**
   * @brief Mutable subscript operator with out-of-bounds safety.
   *
   * @param index 0-indexed column position.
   * @return Mutable reference to the element (or fallback static null if
   * invalid).
   */
  inline reference operator[](int index) noexcept {
    return (m_data && index >= 0 && index < m_len) ? m_data[index]
                                                   : mutable_fallback_null();
  }
  inline reference operator[](size_type index) noexcept {
    return (m_data && index < static_cast<size_type>(m_len))
               ? m_data[index]
               : mutable_fallback_null();
  }

  /**
   * @brief Read-only subscript operator with out-of-bounds safety.
   *
   * @param index 0-indexed column position.
   * @return Const reference to the element (or fallback static null if
   * invalid).
   */
  inline const_reference operator[](int index) const noexcept {
    return (m_data && index >= 0 && index < m_len) ? m_data[index]
                                                   : fallback_null();
  }
  inline const_reference operator[](size_type index) const noexcept {
    return (m_data && index < static_cast<size_type>(m_len)) ? m_data[index]
                                                             : fallback_null();
  }

  /** @brief Bounds-safe mutable element accessor identical to operator[]. */
  inline reference at(int index) noexcept { return (*this)[index]; }
  inline reference at(size_type index) noexcept { return (*this)[index]; }

  /** @brief Bounds-safe read-only element accessor identical to operator[]. */
  inline const_reference at(int index) const noexcept { return (*this)[index]; }
  inline const_reference at(size_type index) const noexcept {
    return (*this)[index];
  }

  /** @brief Forward and bidirectional iterators. */
  inline iterator begin() noexcept { return m_data; }
  inline const_iterator begin() const noexcept { return m_data; }
  inline const_iterator cbegin() const noexcept { return m_data; }
  inline iterator end() noexcept { return m_data + m_len; }
  inline const_iterator end() const noexcept { return m_data + m_len; }
  inline const_iterator cend() const noexcept { return m_data + m_len; }

  /** @brief Reverse iterators. */
  inline reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  inline const_reverse_iterator rbegin() const noexcept {
    return const_reverse_iterator(end());
  }
  inline const_reverse_iterator crbegin() const noexcept {
    return const_reverse_iterator(cend());
  }
  inline reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  inline const_reverse_iterator rend() const noexcept {
    return const_reverse_iterator(begin());
  }
  inline const_reverse_iterator crend() const noexcept {
    return const_reverse_iterator(cbegin());
  }

  /** @brief Fills all elements with a clone of val. */
  inline void fill(const SqliteValueOwned &val) {
    for (int i = 0; i < m_len; ++i)
      m_data[i] = val.clone();
  }

  /** @brief Fills all elements with a primitive value. */
  template <
      typename TPrimitive,
      typename sqlite_enable_if<
          !sqlite_is_same<typename sqlite_remove_reference<TPrimitive>::type,
                          SqliteValueOwned>::value,
          int>::type = 0>
  inline void fill(const TPrimitive &val) {
    for (int i = 0; i < m_len; ++i)
      m_data[i] = SqliteValueOwned(val);
  }

  // Typed Column Extraction Accessors, Composite Hashing & Legacy Iterator
  SQLITE_DERIVE_ARRAY_ACCESSORS
  SQLITE_DERIVE_ARRAY_HASH

public:
  /** @brief Converts this wrapper span to a non-owning SqliteRowOwnedView. */
  inline SqliteRowOwnedView to_view() const noexcept;

  // ========================================================================
  // Full Relational Operators (==, !=, <, <=, >, >=)
  // =========================================================================
  SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteRowOwnedWrapper)
  SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteRowOwnedView)
  SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteRowView)
  SQLITE_DERIVE_ALL_SCALAR_RELATIONAL_OPS
};

static_assert(sizeof(SqliteRowOwnedWrapper) == 16,
              "SqliteRowOwnedWrapper must be exactly 16 bytes!");

/**
 * @brief Canonical alias for SqliteRowOwnedWrapper.
 */
typedef SqliteRowOwnedWrapper SqliteRowOwned;

// Inline implementations for SqliteRowOwnedView constructors dependent on SqliteRowOwnedWrapper
inline SqliteRowOwnedView::SqliteRowOwnedView(const SqliteRowOwnedWrapper &wrap) noexcept
    : m_array(wrap.data()), m_col_count(wrap.size()),
      m_source(wrap.data() && wrap.size() > 0 ? SQLITE_ROW_OWNED_SOURCE_ARRAY
                                              : SQLITE_ROW_OWNED_SOURCE_EMPTY) {}

inline SqliteRowOwnedView SqliteRowOwnedWrapper::to_view() const noexcept {
  return SqliteRowOwnedView(*this);
}

// Symmetric reverse operators
SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteRowView, SqliteRowOwnedWrapper)
SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteRowOwnedView, SqliteRowOwnedWrapper)
SQLITE_DERIVE_ALL_REVERSE_RELATIONAL_OPS(SqliteRowOwnedWrapper)

namespace SqliteRowUtil {
inline void copy_from_wrapper(SqliteValueOwned *dest,
                              const SqliteRowOwnedWrapper &view,
                              int count) noexcept {
  for (int i = 0; i < count; ++i) {
    dest[i] = view[i].clone();
  }
}
} // namespace SqliteRowUtil

// ============================================================================
// 4. Transparent Functors for Swiss Tables & B-Trees (SqliteRow*)
// ============================================================================

#ifndef SQLITE_DERIVE_TRANSPARENT_ROW_HASH_OVERLOADS
#define SQLITE_DERIVE_TRANSPARENT_ROW_HASH_OVERLOADS                           \
  inline size_t operator()(const SqliteRowOwnedWrapper &k) const noexcept {    \
    return static_cast<size_t>(k.hash());                                      \
  }                                                                            \
  inline size_t operator()(const SqliteRowOwnedView &k) const noexcept {       \
    return static_cast<size_t>(k.hash());                                      \
  }                                                                            \
  inline size_t operator()(const SqliteRowView &r) const noexcept {            \
    return static_cast<size_t>(r.hash());                                      \
  }                                                                            \
  template <size_t N, typename Enable>                                         \
  inline size_t operator()(const SqliteValueTuple<N, Enable> &r)               \
      const noexcept {                                                         \
    return static_cast<size_t>(r.hash());                                      \
  }                                                                            \
  template <size_t N, typename Enable>                                         \
  inline size_t operator()(const SqliteValueVec<N, Enable> &r)                 \
      const noexcept {                                                         \
    return static_cast<size_t>(r.hash());                                      \
  }                                                                            \
  SQLITE_DERIVE_TRANSPARENT_SCALAR_HASH_OVERLOADS
#endif

/**
 * @struct SqliteRowHash
 * @brief Transparent 64-bit MurmurHash2 functor for row spans, containers, and
 * primitives.
 *
 * Enables zero-allocation heterogeneous hashing across SqliteRowOwnedWrapper
 * spans, SqliteValueTuple<N>, SqliteValueVec<N>, SqliteRowView, strings, blobs,
 * and primitives.
 */
struct SqliteRowHash {
  using is_transparent = void;
  SQLITE_DERIVE_TRANSPARENT_ROW_HASH_OVERLOADS
};

/**
 * @struct SqliteRowEqual
 * @brief Transparent equality functor for Swiss tables and hash map containers.
 */
SQLITE_DERIVE_TRANSPARENT_EQUAL(SqliteRowEqual)

/**
 * @struct SqliteRowLess
 * @brief Transparent less-than functor for B-Tree and ordered map containers.
 */
SQLITE_DERIVE_TRANSPARENT_LESS(SqliteRowLess)

// Core container integration (SqliteValueTuple, SqliteValueVec,
// withSqliteRowOwned)
#include "sqlite3_value_containers.hpp"

#endif // SQLITE3_ROW_HPP
