# Unified C++ Macro Architecture (`docs/MACROS.md`)

This document provides a comprehensive technical reference for the macro synthesizer suite implemented across `sqlite-ext-core`. These macros eliminate boilerplate, guarantee zero-overhead inlining, enforce strict SQLite type-rank collation semantics, and synthesize standard `std::array` / `std::vector` compliant interfaces, C++11 forward and reverse iterators, bidirectional relational operators, and C++20 transparent functors.

---

## Architecture Overview

The macro suite is organized into clean functional tiers:

```
┌───────────────────────────────────────────────────────────────────────────┐
│       5. MATRIX DISPATCH, SCOPE ALLOCATION & VALUE CONTAINERS             │
│  (include/sqlite3_value_containers.hpp)                                   │
│  SQLITE_DISPATCH_1D_8                  SQLITE_DISPATCH_2D_8X8             │
│  SQLITE_WITH_ROW_OWNED_1D              SQLITE_WITH_KEY_VAL_OWNED_8X8       │
│  SQLITE_MAKE_STORAGE_8X8               SQLITE_MAKE_DEFAULT_STORAGE_8X8    │
│  SQLITE_DERIVE_STD_TUPLE_MODIFIERS     SQLITE_DERIVE_STD_VEC_METHODS      │
│  SQLITE_DERIVE_PRIMITIVE_CONSTRUCTORS  SQLITE_DERIVE_HETEROGENEOUS_CTORS  │
├───────────────────────────────────────────────────────────────────────────┤
│                           4. TRANSPARENT FUNCTORS                         │
│  (include/sqlite3_value.hpp & include/sqlite3_row.hpp)                    │
│  SQLITE_DERIVE_TRANSPARENT_EQUAL       SQLITE_DERIVE_TRANSPARENT_LESS     │
│  SQLITE_DERIVE_TRANSPARENT_SCALAR_HASH SQLITE_DERIVE_TRANSPARENT_ROW_HASH │
├───────────────────────────────────────────────────────────────────────────┤
│                     3. ROW & CONTAINER RELATIONAL OPS                     │
│  (include/sqlite3_row.hpp)                                                │
│  SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS   SQLITE_DERIVE_SCALAR_RELOPS     │
│  SQLITE_DERIVE_ALL_SCALAR_RELATIONAL_OPS  SQLITE_DERIVE_REVERSE_RELATIONAL_OPS │
│  SQLITE_DERIVE_ALL_REVERSE_RELATIONAL_OPS                                  │
├───────────────────────────────────────────────────────────────────────────┤
│               2. TYPED EXTRACTION & COMPOSITE HASHING                     │
│  (include/sqlite3_row.hpp)                                                │
│  SQLITE_DERIVE_ARRAY_ACCESSORS         SQLITE_DERIVE_ARRAY_HASH           │
├───────────────────────────────────────────────────────────────────────────┤
│               1. ARRAY ACCESSORS, ITERATORS & STANDARD ALIGNMENT          │
│  (include/sqlite3_row.hpp)                                                │
│  sqlite_reverse_iterator<Iter>         sqlite_random_access_iterator_tag  │
│  SQLITE_DERIVE_STANDARD_CONTAINER_TYPEDEFS                                │
│  SQLITE_DERIVE_ARRAY_ITERATORS         SQLITE_DERIVE_ARRAY_ITERATOR       │
│  SQLITE_DERIVE_ARRAY_ELEMENT_ACCESSORS SQLITE_DERIVE_STD_ARRAY_METHODS     │
└───────────────────────────────────────────────────────────────────────────┘
```

---

## 1. Array & Standard Container Synthesis Suite (`include/sqlite3_row.hpp`)

These macros are applied inside row and container class definitions to synthesize standard container member types, element accessors, composite MurmurHash2 computation, and standard forward/reverse iterators.

### 1.1 `SQLITE_DERIVE_STANDARD_CONTAINER_TYPEDEFS`

Synthesizes standard C++ container member types:

```cpp
#define SQLITE_DERIVE_STANDARD_CONTAINER_TYPEDEFS(ValType, RefType, ConstRefType, PtrType, ConstPtrType, IterType, ConstIterType) \
    typedef ValType                                                     value_type; \
    typedef size_t                                                      size_type; \
    typedef ptrdiff_t                                                   difference_type; \
    typedef RefType                                                     reference; \
    typedef ConstRefType                                                const_reference; \
    typedef PtrType                                                     pointer; \
    typedef ConstPtrType                                                const_pointer; \
    typedef IterType                                                    iterator; \
    typedef ConstIterType                                               const_iterator; \
    typedef sqlite_reverse_iterator<iterator>                           reverse_iterator; \
    typedef sqlite_reverse_iterator<const_iterator>                     const_reverse_iterator;
```

### 1.2 `SQLITE_DERIVE_ARRAY_ITERATORS(DataPtr, SizeVal)`

Synthesizes inlined forward and reverse iterator factory accessors:

```cpp
#define SQLITE_DERIVE_ARRAY_ITERATORS(DataPtr, SizeVal) \
    inline iterator               begin() noexcept { return (DataPtr); } \
    inline const_iterator         begin() const noexcept { return (DataPtr); } \
    inline const_iterator         cbegin() const noexcept { return (DataPtr); } \
    inline iterator               end() noexcept { return (DataPtr) + (SizeVal); } \
    inline const_iterator         end() const noexcept { return (DataPtr) + (SizeVal); } \
    inline const_iterator         cend() const noexcept { return (DataPtr) + (SizeVal); } \
    inline reverse_iterator       rbegin() noexcept { return reverse_iterator(end()); } \
    inline const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); } \
    inline const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); } \
    inline reverse_iterator       rend() noexcept { return reverse_iterator(begin()); } \
    inline const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); } \
    inline const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }
```

### 1.3 `SQLITE_DERIVE_ARRAY_ELEMENT_ACCESSORS(DataPtr, SizeVal, FallbackNull)`

Synthesizes standard `std::array` and `std::vector` element accessors (`front()`, `back()`, `at()`, `operator[]`) with safe out-of-bounds fallback to canonical `SQLITE_NULL`:

```cpp
#define SQLITE_DERIVE_ARRAY_ELEMENT_ACCESSORS(DataPtr, SizeVal, FallbackNull) \
    inline reference       front() noexcept { return (SizeVal) > 0 ? (DataPtr)[0] : (FallbackNull); } \
    inline const_reference front() const noexcept { return (SizeVal) > 0 ? (DataPtr)[0] : (FallbackNull); } \
    inline reference       back() noexcept { return (SizeVal) > 0 ? (DataPtr)[(SizeVal) - 1] : (FallbackNull); } \
    inline const_reference back() const noexcept { return (SizeVal) > 0 ? (DataPtr)[(SizeVal) - 1] : (FallbackNull); } \
    inline reference       at(size_type pos) noexcept { return (pos < static_cast<size_type>(SizeVal)) ? (DataPtr)[pos] : (FallbackNull); } \
    inline const_reference at(size_type pos) const noexcept { return (pos < static_cast<size_type>(SizeVal)) ? (DataPtr)[pos] : (FallbackNull); } \
    inline reference       operator[](int idx) noexcept { return (idx >= 0 && idx < static_cast<int>(SizeVal)) ? (DataPtr)[idx] : (FallbackNull); } \
    inline const_reference operator[](int idx) const noexcept { return (idx >= 0 && idx < static_cast<int>(SizeVal)) ? (DataPtr)[idx] : (FallbackNull); } \
    inline reference       operator[](size_type idx) noexcept { return (idx < static_cast<size_type>(SizeVal)) ? (DataPtr)[idx] : (FallbackNull); } \
    inline const_reference operator[](size_type idx) const noexcept { return (idx < static_cast<size_type>(SizeVal)) ? (DataPtr)[idx] : (FallbackNull); }
```

### 1.4 `SQLITE_DERIVE_STD_ARRAY_METHODS(DataPtr, SizeVal, FallbackNull, MaxSizeVal)`

Composite macro bundling `SQLITE_DERIVE_ARRAY_ITERATORS`, `SQLITE_DERIVE_ARRAY_ELEMENT_ACCESSORS`, and `max_size()`.

### 1.5 `sqlite_reverse_iterator<Iter>`

Standard-compliant bidirectional/random-access reverse iterator adapter supporting both lvalue references (`SqliteValueOwned&`) and temporary views (`SqliteValueView`) via an internal `ArrowProxy`.

---

## 2. Typed Extraction & Composite Hashing Suite (`include/sqlite3_row.hpp`)

### 2.1 `SQLITE_DERIVE_ARRAY_ACCESSORS`

Synthesizes uniform, zero-overhead convenience accessors for integer, float, text, blob, and type introspection with a default `col = 0`:

```cpp
#define SQLITE_DERIVE_ARRAY_ACCESSORS \
    inline sqlite3_int64 as_int64(int col = 0) const noexcept { return (*this)[col].as_int64(); } \
    inline int           as_int(int col = 0)   const noexcept { return (*this)[col].as_int(); } \
    inline double        as_double(int col = 0)const noexcept { return (*this)[col].as_double(); } \
    inline SqliteStringView as_text(int col = 0) const noexcept { return (*this)[col].as_text(); } \
    inline SqliteBlobView   as_blob(int col = 0) const noexcept { return (*this)[col].as_blob(); } \
    inline bool          as_bool(int col = 0)   const noexcept { return (*this)[col].as_bool(); } \
    inline int           type(int col = 0)      const noexcept { return (*this)[col].type(); } \
    inline uint8_t       subtype(int col = 0)   const noexcept { return (*this)[col].subtype(); } \
    inline bool          is_null(int col = 0)   const noexcept { return (*this)[col].is_null(); }
```

### 2.2 `SQLITE_DERIVE_ARRAY_HASH`

Synthesizes 64-bit composite MurmurHash2 computation across all columns with an $O(1)$ fast path for 1-column rows:

```cpp
#define SQLITE_DERIVE_ARRAY_HASH \
    inline unsigned long long hash() const noexcept { \
        int sz = this->size(); \
        if (sz == 1) return (*this)[0].hash(); \
        uint64_t h = SqliteHashUtil::DEFAULT_SEED; \
        for (int i = 0; i < sz; ++i) { \
            h = SqliteHashUtil::combine(h, (*this)[i].hash()); \
        } \
        return h; \
    }
```

---

## 3. Container & Scalar Relational Operators (`include/sqlite3_row.hpp`)

These macros generate full relational operator suites (`==`, `!=`, `<`, `<=`, `>`, `>=`) for multi-column and single-column containers.

### 3.1 `SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(ContainerType)`

Synthesizes lexicographical relational comparisons against any other container or self:

```cpp
#define SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(ContainerType) \
    inline bool operator==(const ContainerType &other) const noexcept { \
        if (this->size() != other.size()) return false; \
        int sz = this->size(); \
        for (int i = 0; i < sz; ++i) { \
            if (!((*this)[i] == other[i])) return false; \
        } \
        return true; \
    } \
    inline bool operator!=(const ContainerType &other) const noexcept { return !(*this == other); } \
    inline bool operator<(const ContainerType &other) const noexcept { \
        int sz1 = this->size(), sz2 = other.size(); \
        int min_sz = sz1 < sz2 ? sz1 : sz2; \
        for (int i = 0; i < min_sz; ++i) { \
            if ((*this)[i] < other[i]) return true; \
            if (other[i] < (*this)[i]) return false; \
        } \
        return sz1 < sz2; \
    } \
    inline bool operator>(const ContainerType &other) const noexcept { return other < *this; } \
    inline bool operator<=(const ContainerType &other) const noexcept { return !(other < *this); } \
    inline bool operator>=(const ContainerType &other) const noexcept { return !(*this < other); }
```

### 3.2 `SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(ScalarType)`

Enables single-column containers (`SqliteRowView`, `SqliteRowOwnedView`, `SqliteRowOwnedWrapper`, `SqliteValueTuple<1>`, `SqliteValueVec<N>`) to compare directly against scalar values without constructing a temporary container:

```cpp
#define SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(ScalarType) \
    inline bool operator==(const ScalarType &val) const noexcept { return (this->size() == 1) ? ((*this)[0] == val) : false; } \
    inline bool operator!=(const ScalarType &val) const noexcept { return !(*this == val); } \
    inline bool operator<(const ScalarType &val)  const noexcept { return (this->size() == 1) ? ((*this)[0] < val) : (this->size() < 1); } \
    inline bool operator>(const ScalarType &val)  const noexcept { return (this->size() == 1) ? ((*this)[0] > val) : (this->size() > 1); } \
    inline bool operator<=(const ScalarType &val) const noexcept { return !(*this > val); } \
    inline bool operator>=(const ScalarType &val) const noexcept { return !(*this < val); }
```

### 3.3 `SQLITE_DERIVE_ALL_SCALAR_RELATIONAL_OPS`

Bundles all 11 supported scalar and primitive types into a single class-level invocation (`SqliteValueOwned`, `SqliteValueView`, `SqliteStringView`, `SqliteStringOwned`, `SqliteBlobView`, `SqliteBlobOwned`, `sqlite3_int64`, `long`, `int`, `unsigned int`, `unsigned long`, `unsigned long long`, `double`, `bool`, `const char*`).

### 3.4 `SQLITE_DERIVE_ALL_REVERSE_RELATIONAL_OPS(TargetClass)`

Generates symmetric global non-member reverse relational operators (`scalar OP container` $\rightarrow$ `container reverse_OP scalar`):

```cpp
#define SQLITE_DERIVE_ALL_REVERSE_RELATIONAL_OPS(TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteValueOwned, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteValueView, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteStringView, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteStringOwned, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteBlobView, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteBlobOwned, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(sqlite3_int64, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(long, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(int, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(unsigned int, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(unsigned long, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(unsigned long long, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(double, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(bool, TargetClass) \
    inline bool operator==(const char *lhs, const TargetClass &rhs) noexcept { return rhs == lhs; } \
    inline bool operator!=(const char *lhs, const TargetClass &rhs) noexcept { return rhs != lhs; } \
    inline bool operator<(const char *lhs, const TargetClass &rhs) noexcept { return rhs > lhs; } \
    inline bool operator<=(const char *lhs, const TargetClass &rhs) noexcept { return rhs >= lhs; } \
    inline bool operator>(const char *lhs, const TargetClass &rhs) noexcept { return rhs < lhs; } \
    inline bool operator>=(const char *lhs, const TargetClass &rhs) noexcept { return rhs <= lhs; }
```

---

## 4. Value Container Modifier Suite (`include/sqlite3_value_containers.hpp`)

These macros synthesize standard library container modifiers specific to tuples and dynamic vectors.

### 4.1 `SQLITE_DERIVE_STD_TUPLE_MODIFIERS(DataPtr, SizeVal)`

Synthesizes standard `fill()` methods for fixed-size tuple containers:

```cpp
#define SQLITE_DERIVE_STD_TUPLE_MODIFIERS(DataPtr, SizeVal) \
    inline void fill(const SqliteValueOwned& val) { \
        size_type sz = static_cast<size_type>(SizeVal); \
        for (size_type i = 0; i < sz; ++i) (DataPtr)[i] = val.clone(); \
    } \
    template <typename TPrimitive, typename sqlite_enable_if<!sqlite_is_same<typename sqlite_remove_reference<TPrimitive>::type, SqliteValueOwned>::value, int>::type = 0> \
    inline void fill(const TPrimitive& val) { \
        size_type sz = static_cast<size_type>(SizeVal); \
        for (size_type i = 0; i < sz; ++i) (DataPtr)[i] = SqliteValueOwned(val); \
    }
```

### 4.2 `SQLITE_DERIVE_STD_VEC_METHODS(ContainerType)`

Synthesizes complete `std::vector` compliant modifiers:
- `max_size()`
- `resize(size_type count, const SqliteValueOwned& val)` / `resize(size_type count, const TPrimitive& val)`
- `insert(pos, val)` (copy, move, primitive overloads)
- `insert(pos, count, val)`
- `erase(pos)`, `erase(first, last)`
- `assign(count, val)`, `assign(first, last)` (iterator ranges)
- `swap(ContainerType& other)`

---

## 5. Transparent Functors for STL & Swiss Tables

Synthesizes transparent hash, equality, and ordering structs with `using is_transparent = void;` (`SqliteRowHash`, `SqliteRowEqual`, `SqliteRowLess`, `SqliteValueHash`, `SqliteValueEqual`, `SqliteValueLess`).

---

## 6. Generic $8 \times 8$ Compile-Time Matrix Dispatch & Scope Suite (`include/sqlite3_value_containers.hpp`)

- `SQLITE_DISPATCH_1D_8(N, runtime_count, ...)`: Dispatches runtime column count ($1 \dots 8$) to compile-time `constexpr size_t N` (falls back to $N = 0$ for dynamic heap).
- `SQLITE_DISPATCH_2D_8X8(KeyN, ValN, pk_count, val_count, ...)`: Dispatches runtime 2D grid ($8 \times 8 = 64$ combinations).
- `SQLITE_WITH_ROW_OWNED_1D(var, count, ...)`: Dispatches runtime count to stack-allocated `SqliteRowOwnedWrapper` span.
- `SQLITE_WITH_KEY_VAL_OWNED_8X8(key, val, pk_count, val_count, ...)`: Dispatches 2D runtime counts to key/val `SqliteRowOwnedWrapper` spans.
- `SQLITE_MAKE_STORAGE_8X8`: 1-line heap factory macro instantiating container specializations via `sqlite_new`.

---

## 7. Class Adoption Matrix

| Class | Standard Alignment | Iterators | Element Accessors | Typed Accessors | Relational Operators | Reverse Relational Operators | Transparent Functors |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **`SqliteValueOwned`** | N/A | N/A | N/A | Member | `SQLITE_DEF_VAL_*_OPS` | Inlined | `SqliteValueHash`, `SqliteValueEqual`, `SqliteValueLess` |
| **`SqliteValueView`** | N/A | N/A | N/A | Member | `SQLITE_DEF_VAL_*_OPS` | Inlined | `SqliteValueHash`, `SqliteValueEqual`, `SqliteValueLess` |
| **`SqliteRowView`** | `std::array` | Forward + Reverse | `front`, `back`, `at`, `[]` | `SQLITE_DERIVE_ARRAY_ACCESSORS` | Container + All Scalar | `SQLITE_DERIVE_ALL_REVERSE_RELATIONAL_OPS` | Overloaded in `SqliteRowHash` |
| **`SqliteRowOwnedView`** | `std::array` | Forward + Reverse | `front`, `back`, `at`, `[]` | `SQLITE_DERIVE_ARRAY_ACCESSORS` | Container + All Scalar | `SQLITE_DERIVE_ALL_REVERSE_RELATIONAL_OPS` | Overloaded in `SqliteRowHash` |
| **`SqliteRowOwnedWrapper`** | `std::array` | Forward + Reverse | `front`, `back`, `at`, `[]` | `SQLITE_DERIVE_ARRAY_ACCESSORS` | Container + All Scalar | `SQLITE_DERIVE_ALL_REVERSE_RELATIONAL_OPS` | Overloaded in `SqliteRowHash` |
| **`SqliteValueTuple<N>`** | `std::array` + `fill()` | Forward + Reverse | `front`, `back`, `at`, `[]` | `SQLITE_DERIVE_ARRAY_ACCESSORS` | Container + All Scalar | `SQLITE_DERIVE_ALL_REVERSE_RELATIONAL_OPS` | `SqliteRowHash`, `SqliteRowEqual`, `SqliteRowLess` |
| **`SqliteValueVec<N>`** | `std::vector` | Forward + Reverse | `front`, `back`, `at`, `[]` | `SQLITE_DERIVE_ARRAY_ACCESSORS` | Container + All Scalar | `SQLITE_DERIVE_ALL_REVERSE_RELATIONAL_OPS` | `SqliteRowHash`, `SqliteRowEqual`, `SqliteRowLess` |

