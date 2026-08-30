# Unified C++ Macro Architecture (`docs/MACROS.md`)

This document provides a comprehensive technical reference for the macro synthesizer suite implemented across `sqlite-ext-core`. These macros eliminate boilerplate, guarantee zero-overhead inlining, enforce strict SQLite type-rank collation semantics, and synthesize standard `std::array` / `std::vector` compliant interfaces, C++11 forward and reverse iterators, and C++20 transparent functors.

---

## Architecture Overview

The macro suite is organized into clean functional tiers:

```
┌───────────────────────────────────────────────────────────────────────────┐
│               5. 8x8 COMPILE-TIME MATRIX DISPATCH SUITE                   │
│  (include/sqlite3_dispatch_8x8.hpp)                                       │
│  SQLITE_DISPATCH_1D_8                  SQLITE_DISPATCH_2D_8X8             │
│  SQLITE_MAKE_STORAGE_8X8               SQLITE_MAKE_DEFAULT_STORAGE_8X8    │
├───────────────────────────────────────────────────────────────────────────┤
│               4. VALUE CONTAINER MODIFIERS & CONSTRUCTORS                 │
│  (include/sqlite3_value_containers.hpp)                                   │
│  SQLITE_DERIVE_STD_TUPLE_MODIFIERS     SQLITE_DERIVE_STD_VEC_METHODS      │
│  SQLITE_DERIVE_PRIMITIVE_CONSTRUCTORS  SQLITE_DERIVE_HETEROGENEOUS_CTORS  │
├───────────────────────────────────────────────────────────────────────────┤
│                           3. TRANSPARENT FUNCTORS                         │
│  (include/sqlite3_value.hpp & include/sqlite3_row.hpp)                    │
│  SQLITE_DERIVE_TRANSPARENT_EQUAL       SQLITE_DERIVE_TRANSPARENT_LESS     │
│  SQLITE_DERIVE_TRANSPARENT_SCALAR_HASH SQLITE_DERIVE_TRANSPARENT_ROW_HASH │
├───────────────────────────────────────────────────────────────────────────┤
│                     2. ROW & CONTAINER RELATIONAL OPS                     │
│  (include/sqlite3_row.hpp)                                                │
│  SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS   SQLITE_DERIVE_SCALAR_RELOPS     │
│  SQLITE_DERIVE_ALL_SCALAR_RELATIONAL_OPS  SQLITE_DERIVE_ALL_REVERSE_OPS   │
├───────────────────────────────────────────────────────────────────────────┤
│               1. ARRAY ACCESSORS, ITERATORS & STANDARD ALIGNMENT          │
│  (include/sqlite3_row.hpp)                                                │
│  sqlite_reverse_iterator<Iter>         sqlite_random_access_iterator_tag  │
│  SQLITE_DERIVE_STANDARD_CONTAINER_TYPEDEFS                                │
│  SQLITE_DERIVE_ARRAY_ITERATORS         SQLITE_DERIVE_ARRAY_ITERATOR       │
│  SQLITE_DERIVE_ARRAY_ELEMENT_ACCESSORS SQLITE_DERIVE_ARRAY_ACCESSORS      │
│  SQLITE_DERIVE_STD_ARRAY_METHODS       SQLITE_DERIVE_ARRAY_HASH           │
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

## 2. Value Container Modifier Suite (`include/sqlite3_value_containers.hpp`)

These macros synthesize standard library container modifiers specific to tuples and dynamic vectors.

### 2.1 `SQLITE_DERIVE_STD_TUPLE_MODIFIERS(DataPtr, SizeVal)`

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

### 2.2 `SQLITE_DERIVE_STD_VEC_METHODS(ContainerType)`

Synthesizes complete `std::vector` compliant modifiers:
- `max_size()`
- `resize(size_type count, const SqliteValueOwned& val)` / `resize(size_type count, const TPrimitive& val)`
- `insert(pos, val)` (copy, move, primitive overloads)
- `insert(pos, count, val)`
- `erase(pos)`, `erase(first, last)`
- `assign(count, val)`, `assign(first, last)` (iterator ranges)
- `swap(ContainerType& other)`

---

## 3. Container Relational Operators (`include/sqlite3_row.hpp`)

These macros generate full relational operator suites (`==`, `!=`, `<`, `<=`, `>`, `>=`) for multi-column and single-column containers.

### 3.1 `SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(ContainerType)`

Synthesizes lexicographical relational comparisons against any other container or self.

### 3.2 `SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(ScalarType)`

Enables single-column containers (`SqliteRowView`, `SqliteValueTuple<1>`, `SqliteValueVec<N>`, `SqliteRowOwnedWrapper`) to compare directly against scalar values without constructing a temporary container.

### 3.3 `SQLITE_DERIVE_ALL_SCALAR_RELATIONAL_OPS`

Bundles all 11 supported scalar and primitive types into a single invocation.

---

## 4. Transparent Functors for STL & Swiss Tables

Synthesizes transparent hash, equality, and ordering structs with `using is_transparent = void;` (`SqliteRowHash`, `SqliteRowEqual`, `SqliteRowLess`, `SqliteValueHash`, `SqliteValueEqual`, `SqliteValueLess`).

---

## 5. Generic $8 \times 8$ Compile-Time Matrix Dispatch Suite (`include/sqlite3_dispatch_8x8.hpp`)

- `SQLITE_DISPATCH_1D_8(N, runtime_count, ...)`: Dispatches runtime column count ($1 \dots 8$) to compile-time `constexpr size_t N`.
- `SQLITE_DISPATCH_2D_8X8(KeyN, ValN, pk_count, val_count, ...)`: Dispatches runtime 2D grid ($8 \times 8 = 64$ combinations).
- `SQLITE_MAKE_STORAGE_8X8`: 1-line heap factory macro instantiating container specializations via `sqlite_new`.

---

## 6. Class Adoption Matrix

| Class | Standard Alignment | Iterators | Element Accessors | Relational Operators | Transparent Functors |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **`SqliteValueOwned`** | N/A | N/A | N/A | `SQLITE_DEF_VAL_*_OPS` | `SqliteValueHash`, `SqliteValueEqual`, `SqliteValueLess` |
| **`SqliteValueView`** | N/A | N/A | N/A | `SQLITE_DEF_VAL_*_OPS` | `SqliteValueHash`, `SqliteValueEqual`, `SqliteValueLess` |
| **`SqliteRowView`** | `std::array` | Forward + Reverse | `front`, `back`, `at`, `[]` | Container + All Scalar | Overloaded in `SqliteRowHash` |
| **`SqliteRowOwnedWrapper`** | `std::array` | Forward + Reverse | `front`, `back`, `at`, `[]` | Container + All Scalar | Overloaded in `SqliteRowHash` |
| **`SqliteValueTuple<N>`** | `std::array` + `fill()` | Forward + Reverse | `front`, `back`, `at`, `[]` | Container + All Scalar | `SqliteRowHash`, `SqliteRowEqual`, `SqliteRowLess` |
| **`SqliteValueVec<N>`** | `std::vector` | Forward + Reverse | `front`, `back`, `at`, `[]` | Container + All Scalar | `SqliteRowHash`, `SqliteRowEqual`, `SqliteRowLess` |
