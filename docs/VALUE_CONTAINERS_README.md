# C++ Value Containers & Matrix Dispatch (`sqlite3_value_containers.hpp`)

`sqlite-ext-core` provides a unified pair of footprint-optimized, zero-dependency C++11 value container templates alongside generic $8 \times 8$ compile-time matrix dispatchers and scope allocators for multi-column extension engines.

---

## 1. Quick Start

### A. Fixed-Arity Compile-Time Primary Key (`SqliteValueTuple<N>` - std::array compliant)

```cpp
#include "sqlite3_value_containers.hpp"

// Stack-allocated composite key (2 columns = exact 48 bytes, 0 mallocs)
SqliteValueTuple<2> pk(1001LL, "sensor_alpha");

// Standard element accessors & capacity
int id = pk.front().as_int();
SqliteStringView name = pk.back().as_text();
assert(pk.size() == 2);
assert(pk.max_size() == 2);
assert(!pk.empty());

// Standard reverse iteration
for (auto it = pk.rbegin(); it != pk.rend(); ++it) {
    printf("Reverse col type: %d\n", it->type());
}

// In-place fill & swap modifiers
pk.fill(0);

// Convert to non-owning row span
SqliteRowOwnedWrapper span = pk.view();
```

### B. Adaptive Small Buffer Optimized Dynamic Row (`SqliteValueVec<N>` - std::vector compliant)

```cpp
#include "sqlite3_value_containers.hpp"

// SBO stack buffer holding up to 4 elements (4 x 24B = 96 bytes on stack)
SqliteValueVec<4> row;

// 1. Dynamic appending with primitive overloads (0 allocations on stack)
row.push_back(1001);                 // int
row.push_back(23.5);                 // double
row.push_back("temperature");        // const char*
row.emplace_back("sensor_tag");      // emplace in-place

assert(row.size() == 4);
assert(row.is_inline());             // 100% stack data density!

// 2. Standard insertion, erasure, assignment & resize
row.insert(row.begin() + 1, "inserted_val");
row.erase(row.begin() + 2);
row.resize(6, "filler");             // Resizes and spills to heap if > 4

// 3. Pre-allocation, popping, and fast clearing
row.reserve(32);                     // Pre-allocates buffer for >= 32 elements
row.pop_back();                      // Removes last element
row.clear();                         // Resets size to 0 for reuse in hot loops
assert(row.empty());

// 4. Performance Tip: Cache size into a local variable in hot loops
const int row_sz = row.size();       // Avoids repeated tag scanning in tight index loop
for (int i = 0; i < row_sz; ++i) {
    // process row[i]...
}

// 5. Optimal Range-Based for Loop (evaluates begin/end once):
for (const auto& col : row) {        // Const read-only traversal
    printf("Type: %d\n", col.type());
}
for (auto& col : row) {              // In-place mutation without reallocating
    col = 42;
}
```

### C. Stack-Allocated Row Scope Dispatcher (`withSqliteRowOwned`)

```cpp
#include "sqlite3_value_containers.hpp"

// Evaluates runtime count: allocates exact SqliteValueTuple<1..8> on the stack
withSqliteRowOwned(num_cols, [&](SqliteRowOwnedWrapper row) {
    for (int i = 0; i < num_cols; ++i) {
        row[i] = SqliteValueOwned(i * 10);
    }
    insert_row_into_vtab(row);
});
```

### D. Generic $8 \times 8$ Compile-Time Dispatch & Scope Macros

```cpp
#include "sqlite3_value_containers.hpp"

// 1. Shorthand 8x8 factory instantiating any of 64 template combinations:
ITableStorage* create_storage(int total_cols, int pk_count, const int* pk_indices) {
    int val_count = total_cols - pk_count;
    SQLITE_MAKE_DEFAULT_STORAGE_8X8(MapTableImpl, pk_count, val_count, total_cols, pk_count, pk_indices);
}

// 2. Direct scope dispatch without wrapper class:
SQLITE_WITH_KEY_VAL_OWNED_8X8(key, val, pk_cnt, val_cnt, {
    key[0] = 1001;
    val[0] = "data";
    insert_vtab_row(key, val);
});
```

---

## 2. Container Feature Comparison

| Feature | `SqliteValueTuple<N>` | `SqliteValueVec<N>` | `SqliteRowOwnedWrapper` | `SqliteRowView` (`SqliteUdfArgs`) |
| :--- | :---: | :---: | :---: | :---: |
| **Ownership** | Owning (RAII) | Owning (RAII) | Non-Owning (Span) | Non-Owning (Multi-Source View) |
| **Standard Alignment** | `std::array` | `std::vector` | `std::array` | `std::array` |
| **Stack SBO Limit** | Exact $N \times 24\text{B}$ ($N \le 8$) | $N \times 24\text{B}$ in-situ ($N \le 8$) | 16 Bytes (Pointer + Len) | 16 Bytes (Tagged Union + Len) |
| **Heap Model** | $N = 0$ (`SqliteValueTuple<>`) | Dynamic Spill ($> N$) / $N = 0$ | N/A | N/A |
| **Default Creation** | $N$ active `SQLITE_NULL` elements ($N \in [1..8]$) | 0 active elements (`empty() == true`) | 0 length span | 0 length view |
| **Element Access** | `front`, `back`, `at`, `[]`, `data` | `front`, `back`, `at`, `[]`, `data` | `front`, `back`, `at`, `[]`, `data` | `front`, `back`, `at`, `[]`, `data` |
| **Iterators** | Forward + Reverse (`rbegin`/`rend`) | Forward + Reverse (`rbegin`/`rend`) | Forward + Reverse (`rbegin`/`rend`) | Forward + Reverse (`rbegin`/`rend`) |
| **Modifiers** | `fill(val)`, `swap(other)` | `insert`, `erase`, `assign`, `resize`, `swap` | In-place element mutation | Read-Only |
| **Relational Operators** | Full (`==`, `!=`, `<`, `<=`, `>`, `>=`) | Full (`==`, `!=`, `<`, `<=`, `>`, `>=`) | Full (`==`, `!=`, `<`, `<=`, `>`, `>=`) | Full (`==`, `!=`, `<`, `<=`, `>`, `>=`) |
| **Swiss Table Hashing** | `SqliteRowHash` | `SqliteRowHash` | `SqliteRowHash` | `SqliteRowHash` |
| **Heterogeneous B-Tree** | `SqliteRowLess` | `SqliteRowLess` | `SqliteRowLess` | `SqliteRowLess` |

### 0 Elements (Empty) vs. NULL Elements Semantics

| State / Container | `SqliteValueTuple<N>` ($N \in [1..8]$) | `SqliteValueTuple<0>` (`SqliteValueTuple<>`) | `SqliteValueVec<N>` ($N \in [1..8]$) | `SqliteValueVec<0>` (`SqliteValueVec<>`) |
| :--- | :--- | :--- | :--- | :--- |
| **Default Construction** | `size() == N`, `empty() == false`<br>(All $N$ slots initialized to `SQLITE_NULL`) | `size() == 0`, `empty() == true`<br>(0 heap allocations, `nullptr`) | `size() == 0`, `empty() == true`<br>(All SBO stack slots inactive `0x00`) | `size() == 0`, `empty() == true`<br>(0 heap allocations, `nullptr`) |
| **Sized Construction `(k)`** | N/A (Fixed compile-time $N$) | `size() == k`<br>(Allocates $k$ `SQLITE_NULL` elements) | `size() == k`<br>(Activates $k$ `SQLITE_NULL` elements) | `size() == k`<br>(Allocates $k$ `SQLITE_NULL` elements) |
| **Slot Control Tag** | `0xA0` (Active `SQLITE_NULL`) | `0xA0` (Active `SQLITE_NULL`) | `0x00` (Inactive) vs `0xA0` (Active `NULL`) | `0xA0` (Active `SQLITE_NULL`) |

---

## 3. STL & Associative Container Integration

### Heterogeneous `std::unordered_map` (Swiss Tables)

```cpp
#include <unordered_map>
#include "sqlite3_value_containers.hpp"

// Transparent Swiss Table with multi-column tuple keys
std::unordered_map<SqliteValueTuple<2>, std::string, SqliteRowHash, SqliteRowEqual> cache;

SqliteValueTuple<2> k1(42, "sensor_A");
cache[k1] = "Active Station";

assert(cache.find(k1) != cache.end());
```

### Heterogeneous `std::map` (B-Trees with Zero-Allocation Search)

```cpp
#include <map>
#include "sqlite3_value_containers.hpp"

std::map<SqliteRowOwnedWrapper, int, SqliteRowLess> index;

// Find by scalar integer WITHOUT constructing a temporary key container!
auto it = index.find(42);

// Range query using scalar boundary
auto it_lb = index.lower_bound(100);
```

---

## 4. API Reference

### `SqliteValueTuple<N>` Standard Array Methods

- `size_type size() const noexcept` / `size_type count() const noexcept`: Returns fixed column count $N$.
- `size_type max_size() const noexcept`: Returns fixed column count $N$.
- `bool empty() const noexcept`: Returns true if $N == 0$.
- `reference front() noexcept` / `const_reference front() const noexcept`: Accesses first element.
- `reference back() noexcept` / `const_reference back() const noexcept`: Accesses last element.
- `reference at(size_type pos) noexcept`: Bounds-checked column reference with fallback to static null.
- `SqliteValueOwned* data() noexcept`: Returns contiguous pointer to elements.
- `SqliteValueOwned& operator[](int idx) noexcept`: Subscript access.
- `iterator begin() / end()`, `reverse_iterator rbegin() / rend()`, `cbegin() / cend()`, `crbegin() / crend()`.
- `void fill(const SqliteValueOwned& val)` / `void fill(const TPrimitive& val)`: Replaces all elements with `val`.
- `void swap(SqliteValueTuple<N>& other) noexcept`: In-place swap.
- `SqliteRowOwnedWrapper view() const noexcept`: Returns non-owning span.
- `unsigned long long hash() const noexcept`: 64-bit MurmurHash2 hash value.

### `SqliteValueVec<N>` Standard Vector Methods

- `size_type size() const noexcept` / `size_type column_count() const noexcept`: Returns active element count.
- `size_type max_size() const noexcept`: Returns maximum possible capacity.
- `size_type capacity() const noexcept`: Returns current allocated capacity.
- `bool empty() const noexcept`: Returns true if active size is 0.
- `bool is_inline() const noexcept`: Returns true if operating in stack SBO storage.
- `bool is_heap() const noexcept` / `bool is_heap_allocated() const noexcept`: Returns true if dynamically allocated on heap.
- `reference front() noexcept` / `const_reference front() const noexcept`: Accesses first element.
- `reference back() noexcept` / `const_reference back() const noexcept`: Accesses last element.
- `reference at(size_type pos) noexcept`: Bounds-checked column reference.
- `SqliteValueOwned* data() noexcept`: Returns pointer to active contiguous buffer.
- `void reserve(size_type new_cap)`: Pre-allocates buffer.
- `void shrink_to_fit()`: Compacts dynamic allocation.
- `void resize(size_type count)` / `void resize(size_type count, const SqliteValueOwned& val)`: Resizes vector.
- `iterator insert(const_iterator pos, const SqliteValueOwned& val)` (copy, move, primitive, and count overloads).
- `iterator erase(const_iterator pos)` / `iterator erase(const_iterator first, const_iterator last)`.
- `void assign(size_type count, const SqliteValueOwned& val)` / `assign(first, last)`.
- `void swap(SqliteValueVec<N>& other) noexcept`.
- `void push_back(...)`, `emplace_back(...)`, `pop_back()`, `clear()`.
- `SqliteRowOwnedWrapper view() const noexcept`: Returns non-owning span.
- `unsigned long long hash() const noexcept`: 64-bit MurmurHash2 hash value.

---

## 5. Zero-Overhead In-Situ Tag Scanning (`0x20` Threshold & 100% Data Density)

`SqliteValueVec<N>` achieves 100% data density on the stack (zero bytes wasted on an external size integer).

- **Type Encoding in High Bits (5..7)**: Every valid SQLite datatype (`INTEGER=1`, `FLOAT=2`, `TEXT=3`, `BLOB=4`, `NULL=5`) has a code in [1..5], yielding tag values $\ge \text{0x20}$ (`0b001_00000 = 0x20`).
- **Unused Slots**: Unused/cleared slots are zeroed (`0x00 < 0x20`).
- **Backward Scanning**: `vec.size()` scans backwards checking `tag.is_active()`, finding the exact active column count with 0 heap overhead.

---

## 6. Fallible Container APIs (`SqliteResult`, `SqliteStatus`)

For zero-exception and memory-limited environments:

```cpp
SqliteValueVec<4> vec;

// 1. Fallible push_back returning SqliteStatus
SqliteStatus push_stat = vec.try_push_back("data value");
if (push_stat.is_err()) {
    return push_stat;
}

// 2. Fallible resize and reserve
SqliteStatus res_stat = vec.try_resize(100);
if (res_stat.is_err()) {
    printf("Vector resize failed [%d]: %s\n", res_stat.err_code(), res_stat.err_msg());
}

// 3. Fallible cloning returning SqliteResult
SqliteResult<SqliteValueVec<4>> clone_res = vec.try_clone();
if (clone_res.is_err()) {
    return clone_res.status();
}
SqliteValueVec<4> copy = clone_res.take_value();
```
