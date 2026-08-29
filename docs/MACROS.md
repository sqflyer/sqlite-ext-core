# Unified C++ Macro Architecture (`docs/MACROS.md`)

This document provides a comprehensive technical reference for the macro synthesizer suite implemented across `sqlite-ext-core`. These macros eliminate boilerplate, guarantee zero-overhead inlining, enforce strict SQLite type-rank collation semantics, and synthesize C++11 iterators and C++20 transparent functors.

---

## Architecture Overview

The macro suite is organized into 4 cohesive tiers:

```
┌───────────────────────────────────────────────────────────────────────────┐
│                           4. TRANSPARENT FUNCTORS                         │
│  SQLITE_DERIVE_TRANSPARENT_EQUAL       SQLITE_DERIVE_TRANSPARENT_LESS     │
│  SQLITE_DERIVE_TRANSPARENT_SCALAR_HASH SQLITE_DERIVE_TRANSPARENT_ROW_HASH │
├───────────────────────────────────────────────────────────────────────────┤
│                     3. ROW & CONTAINER RELATIONAL OPS                     │
│  SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS   SQLITE_DERIVE_SCALAR_RELOPS     │
│  SQLITE_DERIVE_ALL_SCALAR_RELATIONAL_OPS  SQLITE_DERIVE_ALL_REVERSE_OPS   │
├───────────────────────────────────────────────────────────────────────────┤
│                   2. ARRAY ACCESSORS, HASHING & ITERATORS                 │
│  SQLITE_DERIVE_ARRAY_ACCESSORS         SQLITE_DERIVE_ARRAY_HASH           │
│  SQLITE_DERIVE_ARRAY_ITERATOR(ContainerType, ElementType)                 │
├───────────────────────────────────────────────────────────────────────────┤
│                   1. SCALAR HETEROGENEOUS OPERATORS                       │
│  SQLITE_DEF_VAL_PRIM_OPS               SQLITE_DEF_STR_OPS                 │
│  SQLITE_DEF_VAL_STR_OPS                SQLITE_DEF_BLOB_OPS                │
│  SQLITE_DEF_VAL_BLOB_OPS               SQLITE_DEF_VAL_VAL_OPS             │
└───────────────────────────────────────────────────────────────────────────┘
```

---

## 1. Array Synthesis Suite (`include/sqlite3_value.hpp`)

These macros are applied inside container and row class definitions to automatically synthesize typed extraction, composite MurmurHash2 computation, and standard C++ range-based for loop iterators.

### 1.1 `SQLITE_DERIVE_ARRAY_ACCESSORS`

Synthesizes inlined, bounds-safe typed extraction methods with default `index = 0` (for ergonomics with single-column keys/rows):

```cpp
#define SQLITE_DERIVE_ARRAY_ACCESSORS \
    inline sqlite3_int64    as_int64(int index = 0) const noexcept { return (*this)[index].as_int64(); } \
    inline int              as_int(int index = 0)   const noexcept { return (*this)[index].as_int(); } \
    inline double           as_double(int index = 0) const noexcept { return (*this)[index].as_double(); } \
    inline SqliteStringView as_text(int index = 0)   const noexcept { return (*this)[index].as_text(); } \
    inline SqliteBlobView   as_blob(int index = 0)   const noexcept { return (*this)[index].as_blob(); } \
    inline bool             as_bool(int index = 0)   const noexcept { return (*this)[index].as_bool(); } \
    inline bool             is_null(int index = 0)   const noexcept { return (*this)[index].is_null(); } \
    inline int              type(int index = 0)      const noexcept { return (*this)[index].type(); } \
    inline uint8_t          subtype(int index = 0)   const noexcept { return (*this)[index].subtype(); }
```

### 1.2 `SQLITE_DERIVE_ARRAY_HASH`

Synthesizes a 64-bit MurmurHash2 composite hash function across any container exposing `.size()` and `operator[]`:

```cpp
#define SQLITE_DERIVE_ARRAY_HASH \
    inline unsigned long long hash() const noexcept { \
        int sz = this->size(); \
        if (sz == 0) return SqliteHashUtil::DEFAULT_SEED; \
        if (sz == 1) return (*this)[0].hash(); \
        unsigned long long h = SqliteHashUtil::DEFAULT_SEED; \
        for (int i = 0; i < sz; ++i) { \
            h = SqliteHashUtil::combine(h, (*this)[i].hash()); \
        } \
        return h; \
    }
```

- **Single Column ($N=1$) Fast-Path**: Directly returns `(*this)[0].hash()` with **0 loop overhead and 0 mixer passes**.
- **Composite Folding ($N \ge 2$)**: Sequentially folds column hashes using `SqliteHashUtil::combine()` ensuring high avalanche mixing without bit cancellation.

### 1.3 `SQLITE_DERIVE_ARRAY_ITERATOR(ContainerType, ElementType)`

Synthesizes a C++11 forward iterator and inlined `begin()` / `end()` methods, enabling native range-based for loops:

```cpp
#define SQLITE_DERIVE_ARRAY_ITERATOR(ContainerType, ElementType) \
    class Iterator { \
    private: \
        const ContainerType* m_array; \
        int                  m_idx; \
    public: \
        inline Iterator(const ContainerType* arr, int idx) noexcept : m_array(arr), m_idx(idx) {} \
        inline ElementType operator*() const noexcept { return (*m_array)[m_idx]; } \
        inline Iterator& operator++() noexcept { ++m_idx; return *this; } \
        inline Iterator operator++(int) noexcept { Iterator tmp = *this; ++m_idx; return tmp; } \
        inline bool operator==(const Iterator& o) const noexcept { return m_idx == o.m_idx && m_array == o.m_array; } \
        inline bool operator!=(const Iterator& o) const noexcept { return !(*this == o); } \
    }; \
    inline Iterator begin() const noexcept { return Iterator(this, 0); } \
    inline Iterator end() const noexcept { return Iterator(this, this->size()); }
```

#### Usage Example:
```cpp
SqliteRowView row = stmt.row();
for (SqliteValueView col : row) {
    printf("Type: %d\n", col.type());
}
```

---

## 2. Container Relational Operators (`include/sqlite3_row.hpp`)

These macros generate full relational operator suites (`==`, `!=`, `<`, `<=`, `>`, `>=`) for multi-column and single-column containers.

### 2.1 `SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(ContainerType)`

Synthesizes lexicographical relational comparisons against any other container or self:

```cpp
#define SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(ContainerType) \
    inline bool operator==(const ContainerType& other) const noexcept { \
        if (this->size() != other.size()) return false; \
        int sz = this->size(); \
        for (int i = 0; i < sz; ++i) { \
            if (!((*this)[i] == other[i])) return false; \
        } \
        return true; \
    } \
    inline bool operator!=(const ContainerType& other) const noexcept { return !(*this == other); } \
    inline bool operator<(const ContainerType& other) const noexcept { \
        int sz1 = this->size(); \
        int sz2 = other.size(); \
        int min_sz = sz1 < sz2 ? sz1 : sz2; \
        for (int i = 0; i < min_sz; ++i) { \
            if ((*this)[i] < other[i]) return true; \
            if (other[i] < (*this)[i]) return false; \
        } \
        return sz1 < sz2; \
    } \
    inline bool operator<=(const ContainerType& other) const noexcept { return !(other < *this); } \
    inline bool operator>(const ContainerType& other) const noexcept { return other < *this; } \
    inline bool operator>=(const ContainerType& other) const noexcept { return !(*this < other); }
```

### 2.2 `SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(ScalarType)`

Enables single-column containers (e.g. `SqliteRowView`, `SqliteRowKeyOwned`, `SqliteRowOwnedWrapper`) to compare directly against scalar values without constructing a temporary container:

```cpp
#define SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(ScalarType) \
    inline bool operator==(const ScalarType& scalar) const noexcept { \
        return this->size() == 1 && (*this)[0] == scalar; \
    } \
    inline bool operator!=(const ScalarType& scalar) const noexcept { return !(*this == scalar); } \
    inline bool operator<(const ScalarType& scalar) const noexcept { \
        if (this->size() == 0) return true; \
        return (*this)[0] < scalar; \
    } \
    inline bool operator<=(const ScalarType& scalar) const noexcept { return !(scalar < *this); } \
    inline bool operator>(const ScalarType& scalar) const noexcept { return scalar < *this; } \
    inline bool operator>=(const ScalarType& scalar) const noexcept { return !(*this < scalar); }
```

### 2.3 `SQLITE_DERIVE_ALL_SCALAR_RELATIONAL_OPS`

Bundles all 11 supported scalar and primitive types into a single invocation:
- `SqliteValueOwned`, `SqliteValueView`
- `SqliteStringView`, `SqliteStringOwned`, `const char*`
- `SqliteBlobView`, `SqliteBlobOwned`
- `sqlite3_int64`, `int`, `double`, `bool`

### 2.4 Symmetric Reverse Operator Macros

- `SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(LhsType, RhsType)`: Generates non-member `operator==(LhsType, RhsType)`, `operator<(LhsType, RhsType)`, etc., delegating to the member operators.
- `SQLITE_DERIVE_ALL_REVERSE_RELATIONAL_OPS(TargetClass)`: Automatically generates reverse operators for all 11 scalar types against `TargetClass`.

---

## 3. Transparent Functors for STL & Swiss Tables

To enable **zero-allocation heterogeneous lookups** in associative containers (`std::map`, `std::unordered_map`, Swiss Tables), these macros synthesize transparent hash, equality, and ordering structs with `using is_transparent = void;`.

### 3.1 `SQLITE_DERIVE_TRANSPARENT_EQUAL(FunctorName)`

```cpp
#define SQLITE_DERIVE_TRANSPARENT_EQUAL(FunctorName) \
    struct FunctorName { \
        using is_transparent = void; \
        template <typename T, typename U> \
        inline bool operator()(const T& a, const U& b) const noexcept { \
            return a == b; \
        } \
    };
```

### 3.2 `SQLITE_DERIVE_TRANSPARENT_LESS(FunctorName)`

```cpp
#define SQLITE_DERIVE_TRANSPARENT_LESS(FunctorName) \
    struct FunctorName { \
        using is_transparent = void; \
        template <typename T, typename U> \
        inline bool operator()(const T& a, const U& b) const noexcept { \
            return a < b; \
        } \
    };
```

### 3.3 `SQLITE_DERIVE_TRANSPARENT_SCALAR_HASH_OVERLOADS`

Synthesizes `operator()` hash overloads for all 11 scalar types:

```cpp
#define SQLITE_DERIVE_TRANSPARENT_SCALAR_HASH_OVERLOADS \
    inline size_t operator()(const SqliteValueOwned& val) const noexcept  { return static_cast<size_t>(val.hash()); } \
    inline size_t operator()(const SqliteValueView& val) const noexcept   { return static_cast<size_t>(val.hash()); } \
    inline size_t operator()(const SqliteStringView& str) const noexcept  { return static_cast<size_t>(str.hash()); } \
    inline size_t operator()(const SqliteStringOwned& str) const noexcept { return static_cast<size_t>(str.hash()); } \
    inline size_t operator()(const SqliteBlobView& blob) const noexcept   { return static_cast<size_t>(blob.hash()); } \
    inline size_t operator()(const SqliteBlobOwned& blob) const noexcept  { return static_cast<size_t>(blob.hash()); } \
    inline size_t operator()(const char* str) const noexcept              { return static_cast<size_t>(SqliteStringUtil::hash(str, SqliteStringUtil::sqlite_strlen(str))); } \
    inline size_t operator()(sqlite3_int64 i) const noexcept              { return static_cast<size_t>(SqliteHashUtil::hash(&i, sizeof(i))); } \
    inline size_t operator()(int i) const noexcept                        { sqlite3_int64 val = i; return static_cast<size_t>(SqliteHashUtil::hash(&val, sizeof(val))); } \
    inline size_t operator()(double d) const noexcept                     { return static_cast<size_t>(SqliteHashUtil::hash(&d, sizeof(d))); } \
    inline size_t operator()(float f) const noexcept                      { double d = f; return static_cast<size_t>(SqliteHashUtil::hash(&d, sizeof(d))); } \
    inline size_t operator()(bool b) const noexcept                       { sqlite3_int64 val = b ? 1 : 0; return static_cast<size_t>(SqliteHashUtil::hash(&val, sizeof(val))); } \
    inline size_t operator()(uint32_t u) const noexcept                   { sqlite3_int64 val = u; return static_cast<size_t>(SqliteHashUtil::hash(&val, sizeof(val))); } \
    inline size_t operator()(uint64_t u) const noexcept                   { return static_cast<size_t>(SqliteHashUtil::hash(&u, sizeof(u))); }
```

### 3.4 `SQLITE_DERIVE_TRANSPARENT_ROW_HASH_OVERLOADS`

Synthesizes zero-allocation row hash overloads for all row spans and containers:

```cpp
#define SQLITE_DERIVE_TRANSPARENT_ROW_HASH_OVERLOADS \
    inline size_t operator()(const SqliteRowOwnedWrapper& k) const noexcept { return static_cast<size_t>(k.hash()); } \
    inline size_t operator()(const SqliteRowView& r) const noexcept         { return static_cast<size_t>(r.hash()); } \
    inline size_t operator()(const SqliteValueViewArray& v) const noexcept  { return static_cast<size_t>(v.hash()); } \
    inline size_t operator()(const SqliteRowDynamic& r) const noexcept      { return static_cast<size_t>(SqliteRowOwnedWrapper(r).hash()); } \
    template <size_t N> \
    inline size_t operator()(const SqliteRowStatic<N>& r) const noexcept    { return static_cast<size_t>(SqliteRowOwnedWrapper(r).hash()); } \
    SQLITE_DERIVE_TRANSPARENT_SCALAR_HASH_OVERLOADS
```

---

## 4. Class Adoption Matrix

The table below summarizes how each class in the codebase implements the macro suite:

| Class | Accessors | Hash | Iterator | Relational Operators | Reverse Operators | Transparent Functors |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **`SqliteValueOwned`** | N/A | Member | N/A | `SQLITE_DEF_VAL_*_OPS` | Inline | `SqliteValueHash`, `SqliteValueEqual`, `SqliteValueLess` |
| **`SqliteValueView`** | N/A | Member | N/A | `SQLITE_DEF_VAL_*_OPS` | Inline | `SqliteValueHash`, `SqliteValueEqual`, `SqliteValueLess` |
| **`SqliteValueViewArray`** | Yes | Yes | Yes | `SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS` | Via Rows | Overloaded in `SqliteRowHash` |
| **`SqliteValueOwnedStaticArray<N>`** | Yes | Yes | Yes | Container | Via Wrapper | Overloaded in `SqliteRowHash` |
| **`SqliteValueOwnedDynamicArray`** | Yes | Yes | Yes | Container | Via Wrapper | Overloaded in `SqliteRowHash` |
| **`SqliteRowView`** | Yes | Yes | Yes | Container + All Scalar | `SQLITE_DERIVE_ALL_REVERSE_RELATIONAL_OPS` | Overloaded in `SqliteRowHash` |
| **`SqliteRowOwnedWrapper`** | Yes | Yes | Yes | Container + All Scalar | `SQLITE_DERIVE_ALL_REVERSE_RELATIONAL_OPS` | Overloaded in `SqliteRowHash` |
| **`SqliteRowKeyOwned`** | Yes | Yes | Yes | Container + All Scalar | `SQLITE_DERIVE_ALL_REVERSE_RELATIONAL_OPS` | `SqliteRowKeyHash`, `SqliteRowKeyEqual`, `SqliteRowKeyLess` |

---

## 5. Performance & Safety Guarantees

1. **Zero Allocation Overhead**: All operators and iterators are `inline noexcept` and perform purely register/stack based comparisons without `malloc`, `new`, or heap allocations.
2. **Strict SQLite Type-Rank Collation**:
   $$\text{NULL} (0) < \text{NUMERIC} (1) < \text{TEXT} (2) < \text{BLOB} (3)$$
   Heterogeneous comparisons between scalars strictly honor SQLite's type hierarchy.
3. **Out-of-Bounds Immunity**: Accessing indices $\ge \text{size}()$ returns static fallback `SQLITE_NULL` instances rather than causing memory faults.
4. **Transparent Swiss Table Lookups**: Calling `map.find("key")` or `btree.find(42)` compiles to direct scalar-to-column register comparisons with zero temporary object construction.
