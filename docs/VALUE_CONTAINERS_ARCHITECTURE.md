# Value Containers & Matrix Dispatch Architecture (`sqlite3_value_containers.hpp`)

This document details the internal systems architecture, binary layout, Small Buffer Optimization (SBO) state machine, L1 cache line density calculations, standard `std::array` / `std::vector` alignment, and compile-time matrix dispatch mechanics for multi-column value containers.

---

## 1. Architectural Philosophy & Design Goals

`sqlite-ext-core` extensions often operate on multi-column records:
1. **Primary & Composite Keys**: Fixed number of columns ($N \in [1..8]$), immutable width, heavily queried in hash tables and B-Trees. Aligned to the standard `std::array` interface.
2. **Payload Values & Rows**: Variable or adaptive column counts, subject to dynamic projection, updates, insertions, and erasures. Aligned to the standard `std::vector` interface.

To achieve maximum performance without standard library container overheads (`std::vector`, `std::tuple`), the system introduces two specialized, orthogonal container templates:

```
┌───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                       THE VALUE CONTAINER DUAL ARCHITECTURE                                           │
├──────────────────────────┬───────────────────────────────────────────┬────────────────────────────────────────────────┤
│ Container Type           │ Memory & Allocation Semantics             │ Standard Alignment & Role                      │
├──────────────────────────┼───────────────────────────────────────────┼────────────────────────────────────────────────┤
│ SqliteValueTuple<N = 0>  │ Exact N x 24B stack array for N = 1..8.   │ std::array compliant. Fixed-arity Primary      │
│                          │ 0 heap allocations, 0 capacity overhead.  │ Keys, Composite Index Keys, Fixed Records.     │
│                          │ N = 0 (SqliteValueTuple<>): Direct heap.  │                                                │
├──────────────────────────┼───────────────────────────────────────────┼────────────────────────────────────────────────┤
│ SqliteValueVec<N = 0>    │ 24-byte aligned in-situ SBO stack buffer  │ std::vector compliant. Adaptive payload rows,  │
│                          │ for N = 1..8, spills dynamically to       │ variable-length scratch vectors, reversible    │
│                          │ sqlite3_malloc64 when resized > N.        │ stack-to-heap lifecycle, insert/erase/assign.  │
│                          │ N = 0 (SqliteValueVec<>): Direct heap.    │                                                │
└──────────────────────────┴───────────────────────────────────────────┴────────────────────────────────────────────────┘
```

---

## 2. In-Situ Stack Footprints & L1 Cache Density

Each `SqliteValueOwned` element is exactly 24 bytes. Therefore, stack-allocated containers achieve deterministic boundaries:

```
SqliteValueTuple<1> / SqliteValueVec<1> (24 Bytes):
[ 24B Element 0 ] -> 24 Bytes (In-situ)

SqliteValueTuple<2> / SqliteValueVec<2> (48 Bytes):
[ 24B Element 0 ][ 24B Element 1 ] -> 48 Bytes

SqliteValueTuple<3> / SqliteValueVec<3> (72 Bytes):
[ 24B Element 0 ][ 24B Element 1 ][ 24B Element 2 ] -> 72 Bytes

SqliteValueTuple<4> / SqliteValueVec<4> (96 Bytes):
[ 24B Element 0 ][ 24B Element 1 ][ 24B Element 2 ][ 24B Element 3 ] -> 96 Bytes (1.5 L1 Cache Lines)

SqliteValueTuple<8> / SqliteValueVec<8> (192 Bytes):
[ 8 x 24B Elements ] -> Exactly 3 L1 Cache Lines!
```

---

## 3. Standard Library Container Compliance & Macro Synthesis

Both `SqliteValueTuple` and `SqliteValueVec` are synthesized using modular macro blocks:

```
┌───────────────────────────────────────────────────────────────────────────┐
│               STANDARD CONTAINER SYNTHESIS ARCHITECTURE                   │
├───────────────────────────────────────────────────────────────────────────┤
│ SQLITE_DERIVE_STANDARD_CONTAINER_TYPEDEFS (sqlite3_row.hpp)               │
│ - value_type, size_type, difference_type, reference, pointer, iterators   │
├───────────────────────────────────────────────────────────────────────────┤
│ SQLITE_DERIVE_STD_ARRAY_METHODS (sqlite3_row.hpp)                         │
│ - begin(), end(), cbegin(), cend(), rbegin(), rend(), crbegin(), crend()  │
│ - front(), back(), at(), operator[]                                       │
│ - max_size()                                                              │
├───────────────────────────────────────────────────────────────────────────┤
│ SQLITE_DERIVE_STD_TUPLE_MODIFIERS (sqlite3_value_containers.hpp)          │
│ - fill(const SqliteValueOwned& val), fill(const TPrimitive& val)          │
├───────────────────────────────────────────────────────────────────────────┤
│ SQLITE_DERIVE_STD_VEC_METHODS (sqlite3_value_containers.hpp)              │
│ - resize(count, val), max_size()                                          │
│ - insert(pos, val), insert(pos, count, val)                               │
│ - erase(pos), erase(first, last)                                          │
│ - assign(count, val), assign(first, last)                                 │
│ - swap(other)                                                             │
└───────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Small Buffer Optimization (SBO) State Machine in `SqliteValueVec<N>`

`SqliteValueVec<N>` manages a dual-representation union:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          SqliteValueVec<N> Memory Union                     │
├───────────────────────────────────┬─────────────────────────────────────────┤
│ Mode A: Inline Stack Buffer       │ Mode B: Dynamic Heap Representation     │
│ (Active when size <= N)           │ (Active when size > N)                  │
├───────────────────────────────────┼─────────────────────────────────────────┤
│ m_inline[0]: SqliteValueOwned(24B)│ ptr:      SqliteValueOwned* (8 Bytes)   │
│ m_inline[1]: SqliteValueOwned(24B)│ size:     uint32_t (4 Bytes)            │
│ ...                               │ capacity: uint16_t (2 Bytes)            │
│ m_inline[N-1]: Value(24B)         │ reserved: uint8_t[9] (9 Bytes)          │
│ (Tag at byte 23 >= 0x20)          │ tag:      SqliteOwnedValueTag (1 Byte)  │
│                                   │           (Offset 23, raw == 0x00)      │
│                                   │ pad:      uint8_t[(N*24)-24] (if N > 1) │
└───────────────────────────────────┴─────────────────────────────────────────┘
```

### Reversible Transition Lifecycle:

1. **Initial Stack State**: Constructing `SqliteValueVec<4>` uses 0 heap memory. Elements `0..3` live in `m_inline`.
2. **Heap Spill**: Calling `resize(6)` allocates 6 contiguous elements via `sqlite3_malloc64`, moves existing inline elements to the heap, sets `m_heap.ptr`, and marks `m_heap.tag.raw = 0x00`.
3. **Safe Return to Stack**: Calling `resize(2)` moves the first 2 elements back to `m_inline`, frees the heap buffer via `sqlite_delete_array`, sets `m_heap.ptr = nullptr`, and restores stack operation.

### Storage State Inspection Methods:
- `is_inline() const noexcept`: Returns `true` if elements are stored entirely on the stack within `m_inline`.
- `is_heap() const noexcept` / `is_heap_allocated() const noexcept`: Returns `true` if dynamic heap buffer (`m_heap.ptr != nullptr`) is actively managing storage. Always returns `true` for `SqliteValueVec<0>` with non-zero capacity.

---

## 5. Generic $8 \times 8$ Compile-Time Matrix Dispatchers & Scope Allocators

Virtual tables and in-memory key-value engines (`memkv_map`, `memkv_lru`, `memkv_zset`, `memkv_ring`) determine table schemas at runtime (`pk_count` and `val_count`).

To avoid runtime branching inside inner iteration loops, `sqlite3_value_containers.hpp` provides compile-time expansion and scope-guarded stack allocation:

```cpp
#define SQLITE_DISPATCH_1D_8(N, runtime_count, ...) \
    switch (runtime_count) { \
        case 1:  { constexpr size_t N = 1; __VA_ARGS__; } break; \
        ... \
        case 8:  { constexpr size_t N = 8; __VA_ARGS__; } break; \
        default: { constexpr size_t N = 0; __VA_ARGS__; } break; \
    }

#define SQLITE_DISPATCH_2D_8X8(KeyN, ValN, pk_count, val_count, ...) \
    switch (pk_count) { \
        case 1:  { constexpr size_t KeyN = 1; SQLITE_DISPATCH_1D_8(ValN, val_count, __VA_ARGS__) } break; \
        ... \
        default: { constexpr size_t KeyN = 0; SQLITE_DISPATCH_1D_8(ValN, val_count, __VA_ARGS__) } break; \
    }

// Direct SqliteRowOwnedWrapper scope wrappers:
SQLITE_WITH_ROW_OWNED_1D(row, num_cols, { ... });
SQLITE_WITH_KEY_VAL_OWNED_8X8(key, val, pk_cnt, val_cnt, { ... });
```

---

## 6. The 1-Byte Control Tag Discriminator (`0x20`) & 100% Stack Data Density

### Bitfield Layout of `SqliteOwnedValueTag` (Offset 15)

Each `SqliteValueOwned` element contains a 1-byte control tag at offset 15:

```
 7       6       5       4       3       2       1       0   (Bit Index)
┌───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┐
│     SQLite Type       │ Heap  │   Inline Length (0..14)       │
│  (SQLITE_INTEGER..    │ Flag  │   (for SBO Text & Blobs)      │
│   SQLITE_NULL = 1..5) │       │                               │
└───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┘
```

### The `0x20` Active Threshold

SQLite's 5 storage classes are shifted into bits 5..7 (`type << 5`):
- `SQLITE_INTEGER` (1): `0b001_00000 = 0x20` (32)
- `SQLITE_FLOAT`   (2): `0b010_00000 = 0x40` (64)
- `SQLITE_TEXT`    (3): `0b011_00000 = 0x60` (96)
- `SQLITE_BLOB`    (4): `0b100_00000 = 0x80` (128)
- `SQLITE_NULL`    (5): `0b101_00000 = 0xA0` (160)

Because all valid SQLite datatypes have codes $1 \dots 5$, **any initialized, constructed `SqliteValueOwned` instance always has `raw >= 0x20`**.

Uninitialized slots, cleared elements, and container markers have `type == 0` (`raw == 0x00 < 0x20`).

$$\text{tag.is\_active()} \iff (\text{raw} \ge \text{0x20}) \iff (\text{type}() \in [1..5])$$

### Performance Best Practice: Hoisting `vec.size()` vs. Range-Based `for` Loops

Because `SqliteValueVec<N>` ($N \in [1..8]$) computes its in-situ stack size dynamically via the backwards tag scan (`tag >= 0x20`), invoking `vec.size()` on every single loop iteration (`for (int i = 0; i < vec.size(); ++i)`) re-evaluates the tag scan if the compiler cannot prove memory invariance across the loop body.

#### 1. Hoisting Size to a Variable
```cpp
// Explicit size hoisting
const int sz = vec.size();
for (int i = 0; i < sz; ++i) {
    process(vec[i]);
}
```

#### 2. Range-Based `for` Loops (Compiler-Optimized)
C++11 range-based `for` loops are **mechanically optimal** for both `SqliteValueTuple` and `SqliteValueVec`:
```cpp
for (const auto& col : vec) {
    process(col);
}
```
Under the hood, C++ lowers this into:
```cpp
auto __begin = vec.begin();
auto __end   = vec.end();    // <-- Computed ONCE before the loop enters!
for (; __begin != __end; ++__begin) {
    auto& col = *__begin;
    process(col);
}
```
- `vec.end()` evaluates `m_data + size()` **exactly once** upon loop entry.
- Each iteration executes only a single direct pointer comparison (`__begin != __end`) and pointer increment (`++__begin`), with **0 repeated tag scans** and 0 indexing multiplications.
- Supports in-place mutation: `for (auto& col : vec) { col = 42; }`.
- Supports standard reverse iteration: `for (auto it = vec.rbegin(); it != vec.rend(); ++it)`.

---

## 7. 0 Elements (Empty) vs. NULL Elements Semantics

There are two critical dimensions to understanding **0 vs. NULL** in `sqlite-ext-core`:

### A. Element Count Semantics: 0 Elements (Empty) vs. $N$ NULL Elements

```
┌────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                    0 ELEMENTS VS. NULL ELEMENTS MATRIX                                             │
├──────────────────────────┬───────────────────────────────┬─────────────────────────────┬───────────────────────────┤
│ Container                │ Default Constructor           │ Sized Constructor (count)   │ Initial State             │
├──────────────────────────┼───────────────────────────────┼─────────────────────────────┼───────────────────────────┤
│ SqliteValueTuple<N>      │ SqliteValueTuple<N>()         │ N/A (Fixed Arity N)         │ N Active SQLITE_NULL      │
│ (N in [1..8] Stack)      │ -> size() == N, empty()=false │                             │ elements (0 heap mallocs) │
├──────────────────────────┼───────────────────────────────┼─────────────────────────────┼───────────────────────────┤
│ SqliteValueTuple<0>      │ SqliteValueTuple<>()          │ SqliteValueTuple<>(count)   │ Default: 0 Elements       │
│ (N = 0 Direct Heap)      │ -> size() == 0, empty()=true  │ -> size() == count          │ Sized: count SQLITE_NULL  │
├──────────────────────────┼───────────────────────────────┼─────────────────────────────┼───────────────────────────┤
│ SqliteValueVec<N>        │ SqliteValueVec<N>()           │ SqliteValueVec<N>(count)    │ Default: 0 Elements       │
│ (N in [1..8] SBO Stack)  │ -> size() == 0, empty()=true  │ -> size() == count          │ Sized: count SQLITE_NULL  │
├──────────────────────────┼───────────────────────────────┼─────────────────────────────┼───────────────────────────┤
│ SqliteValueVec<0>        │ SqliteValueVec<>()            │ SqliteValueVec<>(count)     │ Default: 0 Elements       │
│ (N = 0 Direct Heap)      │ -> size() == 0, empty()=true  │ -> size() == count          │ Sized: count SQLITE_NULL  │
└──────────────────────────┴───────────────────────────────┴─────────────────────────────┴───────────────────────────┘
```

1. **`SqliteValueTuple<N>` ($N \in [1..8]$)** models a **fixed-arity record** (like `std::array<SqliteValueOwned, N>`). It never starts empty; all $N$ slots are immediately populated with canonical `SQLITE_NULL` via 128-byte SIMD bursts.
2. **`SqliteValueVec<N>` ($N \in [1..8]$)** models a **dynamic vector** (like `std::vector<SqliteValueOwned>`). It starts with **0 active elements** (`size() == 0`). Its inline stack buffer slots are inactive until populated via `push_back()` or `resize()`.
3. **`SqliteValueTuple<0>` & `SqliteValueVec<0>`** default construct with **0 elements** (`m_data = nullptr, m_size = 0, m_capacity = 0`), allocating only when initialized with an explicit size, `SqliteRowView`, or initializer list.

### B. Control Tag Discriminator: Inactive Slot (`0x00`) vs. Active SQL NULL (`0xA0`)

```
┌─────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│ Control Tag              │ Control Byte (Offset 15)  │ is_active()  │ is_null()  │ Role                     │
├──────────────────────────┼───────────────────────────┼──────────────┼────────────┼──────────────────────────┤
│ Inactive Slot            │ 0x00 (< 0x20)             │ false        │ false      │ Empty capacity in SBO    │
│                          │                           │              │            │ stack buffer. Ignored.   │
├──────────────────────────┼───────────────────────────┼──────────────┼────────────┼──────────────────────────┤
│ Active SQLITE_NULL       │ 0xA0 (>= 0x20, type = 5)  │ true         │ true       │ Real SQL NULL value in   │
│                          │                           │              │            │ a tuple or active vector.│
└──────────────────────────┴───────────────────────────┴──────────────┴────────────┴──────────────────────────┘
```

---

## 8. Summary Comparison Matrix: Tuple vs. Vector

```
┌─────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                   TUPLE VS. VECTOR ARCHITECTURAL MATRIX                                     │
├──────────────────────────┬───────────────────────────────────────────┬──────────────────────────────────────┤
│ Architectural Property   │ SqliteValueTuple<N>                       │ SqliteValueVec<N>                    │
├──────────────────────────┼───────────────────────────────────────────┼──────────────────────────────────────┤
│ Standard Compliance      │ std::array                                │ std::vector                          │
├──────────────────────────┼───────────────────────────────────────────┼──────────────────────────────────────┤
│ Arity & Capacity         │ Fixed, Immutable ($N$ columns)            │ Variable ($0 \le \text{size} \le N$) │
├──────────────────────────┼───────────────────────────────────────────┼──────────────────────────────────────┤
│ Active Slots on Creation │ All $N$ Slots Active (Default SQL NULL)   │ **0 Active Slots (Empty)**           │
├──────────────────────────┼───────────────────────────────────────────┼──────────────────────────────────────┤
│ Size Discovery Mechanism │ Compile-time constant (`constexpr int N`) │ Backwards Tag Scan (`tag >= 0x20`)   │
├──────────────────────────┼───────────────────────────────────────────┼──────────────────────────────────────┤
│ Needs `is_active()`?     │ **No** (Never has inactive slots)         │ **Yes** (Distinguishes active slots) │
├──────────────────────────┼───────────────────────────────────────────┼──────────────────────────────────────┤
│ Container-Level Heap     │ Dynamic Heap ($N = 0$)                    │ Dynamic Runtime Spill ($\text{sz}>N$)│
└──────────────────────────┴───────────────────────────────────────────┴──────────────────────────────────────┘
```
