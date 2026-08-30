# C++ Value Containers & 8x8 Compile-Time Matrix Dispatch (`sqlite3_value_containers.hpp` & `sqlite3_dispatch_8x8.hpp`)

`sqlite-ext-core` provides a unified pair of footprint-optimized, zero-dependency C++11 value container templates alongside generic $8 \times 8$ compile-time matrix dispatchers for multi-column extension engines.

---

## 1. Quick Start

### A. Fixed-Arity Compile-Time Primary Key (`SqliteValueTuple<N>`)

```cpp
#include "sqlite3_value_containers.hpp"

// Stack-allocated composite key (2 columns = exact 32 bytes, 0 mallocs)
SqliteValueTuple<2> pk;
pk[0] = SqliteValueOwned(1001LL);
pk[1] = SqliteValueOwned("sensor_alpha");

// Typed column extraction (zero allocation)
int id = pk.as_int(0);
SqliteStringView name = pk.as_text(1);

// Range-based for loop iteration
for (const SqliteValueOwned& val : pk) {
    printf("Type: %d\n", val.type());
}

// Convert to non-owning row span
SqliteRowOwnedWrapper span = pk.view();
```

### B. Adaptive Small Buffer Optimized Dynamic Row (`SqliteValueVec<N>`)

```cpp
#include "sqlite3_value_containers.hpp"

// SBO stack buffer holding up to 4 elements (64 bytes = 1 L1 cache line)
SqliteValueVec<4> row;

// 1. Dynamic appending with primitive overloads (0 allocations on stack)
row.push_back(1001);                 // int
row.push_back(23.5);                 // double
row.push_back("temperature");        // const char*
row.emplace_back("sensor_tag");      // emplace in-place

assert(row.size() == 4);
assert(row.is_inline());             // 100% stack data density!

// 2. Dynamically spills to heap if grown beyond N (4)
row.push_back(true);                 // 5th element triggers heap spill
assert(row.size() == 5);
assert(!row.is_inline());            // Now on heap via sqlite3_malloc64

// 3. Pre-allocation, popping, and fast clearing
row.reserve(32);                     // Pre-allocates buffer for >= 32 elements
row.pop_back();                      // Removes last element (size becomes 4)
row.clear();                         // Resets size to 0 for reuse in hot loops
assert(row.empty());
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

### D. Generic $8 \times 8$ Compile-Time Dispatch (`sqlite3_dispatch_8x8.hpp`)

```cpp
#include "sqlite3_dispatch_8x8.hpp"

// 1-liner instantiating any of 64 template combinations:
ITableStorage* create_storage(int total_cols, int pk_count, const int* pk_indices) {
    int val_count = total_cols - pk_count;
    SQLITE_MAKE_DEFAULT_STORAGE_8X8(MapTableImpl, pk_count, val_count, total_cols, pk_count, pk_indices);
}
```

---

## 2. Container Feature Comparison

| Feature | `SqliteValueTuple<N>` | `SqliteValueVec<N>` | `SqliteRowOwnedWrapper` | `SqliteRowView` (`SqliteUdfArgs`) |
| :--- | :---: | :---: | :---: | :---: |
| **Ownership** | Owning (RAII) | Owning (RAII) | Non-Owning (Span) | Non-Owning (Multi-Source View) |
| **Stack SBO Limit** | Exact $N \times 16\text{B}$ ($N \le 8$) | $N \times 16\text{B}$ in-situ ($N \le 8$) | 16 Bytes (Pointer + Len) | 24 Bytes (Tagged Union + Len) |
| **Heap Fallback** | $N \ge 9$ (Heap Tuple) | Dynamic Spill ($> N$) | N/A | N/A |
| **Element Modification** | In-place mutable | Grow / Shrink / Append | In-place mutable | Read-Only View |
| **Relational Operators** | Full (`==`, `!=`, `<`, `<=`, `>`, `>=`) | Full (`==`, `!=`, `<`, `<=`, `>`, `>=`) | Full (`==`, `!=`, `<`, `<=`, `>`, `>=`) | Full (`==`, `!=`, `<`, `<=`, `>`, `>=`) |
| **Range Iteration** | `for (const auto& v : c)` | `for (const auto& v : c)` | `for (const auto& v : c)` | `for (SqliteValueView v : c)` |
| **Swiss Table Hashing** | `SqliteRowHash` | `SqliteRowHash` | `SqliteRowHash` | `SqliteRowHash` |
| **Heterogeneous B-Tree** | `SqliteRowLess` | `SqliteRowLess` | `SqliteRowLess` | `SqliteRowLess` |

---

## 3. STL & Associative Container Integration

### Heterogeneous `std::unordered_map` (Swiss Tables)

```cpp
#include <unordered_map>
#include "sqlite3_value_containers.hpp"

// Transparent Swiss Table with multi-column tuple keys
std::unordered_map<SqliteValueTuple<2>, std::string, SqliteRowHash, SqliteRowEqual> cache;

SqliteValueTuple<2> k1;
k1[0] = SqliteValueOwned(42);
k1[1] = SqliteValueOwned("sensor_A");
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

### `SqliteValueTuple<N>` Methods

- `int size() const noexcept` / `int count() const noexcept`: Returns fixed column count $N$.
- `bool empty() const noexcept`: Returns true if $N == 0$.
- `bool is_inline() const noexcept`: Returns true if $N \in [1..8]$.
- `SqliteValueOwned* data() noexcept`: Returns contiguous pointer to elements.
- `SqliteValueOwned& operator[](int idx) noexcept`: Bounds-safe column reference.
- `SqliteRowOwnedWrapper view() const noexcept`: Returns non-owning span.
- `unsigned long long hash() const noexcept`: 64-bit MurmurHash2 hash value.

### `SqliteValueVec<N>` Methods

- `int size() const noexcept` / `int column_count() const noexcept`: Returns current active element count.
- `bool empty() const noexcept`: Returns true if active size is 0.
- `bool is_inline() const noexcept`: Returns true if currently operating on the stack.
- `void resize(int new_count)`: Resizes vector, spilling to heap or returning to stack as appropriate.
- `SqliteValueOwned* data() noexcept`: Returns pointer to active contiguous buffer.
- `SqliteValueOwned& operator[](int idx) noexcept`: Bounds-safe column reference.
- `SqliteRowOwnedWrapper view() const noexcept`: Returns non-owning span.
- `unsigned long long hash() const noexcept`: 64-bit MurmurHash2 hash value.

---

## 5. Zero-Overhead In-Situ Tag Scanning (`0x20` Threshold & 100% Data Density)

`SqliteValueVec<N>` achieves 100% data density on the stack (zero bytes wasted on an external size integer).

- **Type Encoding in High Bits (5..7)**: Every valid SQLite datatype (`INTEGER=1`, `FLOAT=2`, `TEXT=3`, `BLOB=4`, `NULL=5`) has a code in [1..5], yielding tag values $\ge \text{0x20}$ (`0b001_00000 = 0x20`).
- **Unused Slots**: Unused/cleared slots are zeroed (`0x00 < 0x20`).
- **Backward Scanning**: `vec.size()` scans backwards checking `tag.is_active()`, finding the exact active column count with 0 heap overhead.
