# Value Containers & 8x8 Dispatch Architecture (`sqlite3_value_containers.hpp` & `sqlite3_dispatch_8x8.hpp`)

This document details the internal systems architecture, binary layout, Small Buffer Optimization (SBO) state machine, L1 cache line density calculations, and compile-time matrix dispatch mechanics for multi-column value containers.

---

## 1. Architectural Philosophy & Design Goals

`sqlite-ext-core` extensions often operate on multi-column records:
1. **Primary & Composite Keys**: Fixed number of columns ($N \in [1..8]$), immutable width, heavily queried in hash tables and B-Trees.
2. **Payload Values & Rows**: Variable or adaptive column counts, subject to dynamic projection and updates.

To achieve maximum performance without standard library containers (`std::vector`, `std::tuple`), the system introduces two specialized, orthogonal container templates:

```
┌───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                       THE VALUE CONTAINER DUAL ARCHITECTURE                                          │
├──────────────────────────┬───────────────────────────────────────────┬────────────────────────────────────────────────┤
│ Container Type           │ Memory & Allocation Semantics             │ Primary Architectural Role                     │
├──────────────────────────┼───────────────────────────────────────────┼────────────────────────────────────────────────┤
│ SqliteValueTuple<N>      │ Exact N x 16B stack array for N = 1..8.   │ Compile-time fixed-arity Primary Keys,         │
│                          │ 0 heap allocations, 0 capacity overhead. │ Composite Index Keys, Fixed Record Tuples.     │
│                          │ N >= 9: sqlite3_malloc64 dynamic buffer.  │                                                │
├──────────────────────────┼───────────────────────────────────────────┼────────────────────────────────────────────────┤
│ SqliteValueVec<N>        │ 16-byte aligned in-situ SBO stack buffer  │ Adaptive dynamic payload rows, non-PK value    │
│                          │ for N = 1..8, spills dynamically to       │ columns, variable-length scratch vectors,      │
│                          │ sqlite3_malloc64 when resized > N.        │ reversible stack-to-heap lifecycle.            │
│                          │ N >= 9: Direct dynamic heap vector.       │                                                │
└──────────────────────────┴───────────────────────────────────────────┴────────────────────────────────────────────────┘
```

---

## 2. In-Situ Stack Footprints & L1 Cache Density

Each `SqliteValueOwned` element is exactly 16 bytes. Therefore, stack-allocated containers achieve deterministic, cache-aligned boundaries:

```
SqliteValueTuple<1> / SqliteValueVec<1> (16 Bytes):
[ 16B Element 0 ] -> 4 containers per 64-byte L1 Cache Line

SqliteValueTuple<2> / SqliteValueVec<2> (32 Bytes):
[ 16B Element 0 ][ 16B Element 1 ] -> 2 containers per 64-byte L1 Cache Line

SqliteValueTuple<4> / SqliteValueVec<4> (64 Bytes):
[ 16B Element 0 ][ 16B Element 1 ][ 16B Element 2 ][ 16B Element 3 ] -> Exactly 1 L1 Cache Line!

SqliteValueTuple<8> / SqliteValueVec<8> (128 Bytes):
[ 8 x 16B Elements ] -> Exactly 2 L1 Cache Lines!
```

---

## 3. Small Buffer Optimization (SBO) State Machine in `SqliteValueVec<N>`

`SqliteValueVec<N>` manages a dual-representation union:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          SqliteValueVec<N> Memory Union                     │
├───────────────────────────────────┬─────────────────────────────────────────┤
│ Mode A: Inline Stack Buffer       │ Mode B: Dynamic Heap Representation     │
│ (Active when size <= N)           │ (Active when size > N)                  │
├───────────────────────────────────┼─────────────────────────────────────────┤
│ m_inline[0]: SqliteValueOwned(16B)│ ptr:      SqliteValueOwned* (8 Bytes)   │
│ m_inline[1]: SqliteValueOwned(16B)│ size:     uint32_t (4 Bytes)            │
│ ...                               │ capacity: uint16_t (2 Bytes)            │
│ m_inline[N-1]: Value(16B)         │ reserved: uint8_t  (1 Byte)             │
│ (Tag at byte 15 >= 0x20)          │ tag:      SqliteOwnedValueTag (1 Byte)  │
│                                   │           (tag.raw == 0x00, ptr != null)│
└───────────────────────────────────┴─────────────────────────────────────────┘
```

### Reversible Transition Lifecycle:

1. **Initial Stack State**: Constructing `SqliteValueVec<4>` uses 0 heap memory. Elements `0..3` live in `m_inline`.
2. **Heap Spill**: Calling `resize(6)` allocates 6 contiguous elements via `sqlite3_malloc64`, moves existing inline elements to the heap, sets `m_heap.ptr`, and marks `m_heap.tag.raw = 0x00`.
3. **Safe Return to Stack**: Calling `resize(2)` moves the first 2 elements back to `m_inline`, frees the heap buffer via `sqlite_delete_array`, sets `m_heap.ptr = nullptr`, and restores stack operation.

---

## 4. Generic $8 \times 8$ Compile-Time Matrix Dispatcher (`sqlite3_dispatch_8x8.hpp`)

Virtual tables and in-memory key-value engines (`memkv_map`, `memkv_lru`, `memkv_zset`, `memkv_ring`) determine table schemas at runtime (`pk_count` and `val_count`).

To avoid runtime branching inside inner iteration loops, `sqlite3_dispatch_8x8.hpp` provides compile-time expansion:

```cpp
#define SQLITE_DISPATCH_1D_8(N, runtime_count, ...) \
    switch ((runtime_count) <= 0 ? 1 : (runtime_count)) { \
        case 1:  { constexpr size_t N = 1; __VA_ARGS__; } break; \
        ... \
        case 8:  { constexpr size_t N = 8; __VA_ARGS__; } break; \
        default: { constexpr size_t N = 9; __VA_ARGS__; } break; \
    }

#define SQLITE_DISPATCH_2D_8X8(KeyN, ValN, pk_count, val_count, ...) \
    switch ((pk_count) <= 0 ? 1 : (pk_count)) { \
        case 1:  { constexpr size_t KeyN = 1; SQLITE_DISPATCH_1D_8(ValN, val_count, __VA_ARGS__) } break; \
        ... \
        default: { constexpr size_t KeyN = 9; SQLITE_DISPATCH_1D_8(ValN, val_count, __VA_ARGS__) } break; \
    }
```

### Benefits:
1. **Zero Runtime Dispatch in Loops**: Once constructed, container indexing and hashing operate at compile-time fixed offsets.
2. **Universal Compatibility**: Works with `MapTable`, `LruTable`, `ZSetTable`, `RingTable`, and any custom user template.
3. **1-Line Factory Creation**: Replaces 100+ lines of switch boilerplate with `SQLITE_MAKE_DEFAULT_STORAGE_8X8(...)`.

---

## 5. The 1-Byte Control Tag Discriminator (`0x20`) & 100% Stack Data Density

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

---

## 6. Architectural Distinction: `is_active()` vs. `is_heap()` in Tuples and Vectors

A critical design distinction in `sqlite-ext-core` is why `is_active()` is used by `SqliteValueVec<N>`, why `SqliteValueTuple<N>` never needs it, and how `is_heap()` functions across both containers:

```
┌─────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                   TUPLE VS. VECTOR ARCHITECTURAL MATRIX                                     │
├──────────────────────────┬───────────────────────────────────────────┬──────────────────────────────────────┤
│ Architectural Property   │ SqliteValueTuple<N>                       │ SqliteValueVec<N>                    │
├──────────────────────────┼───────────────────────────────────────────┼──────────────────────────────────────┤
│ Arity & Capacity         │ Fixed, Immutable ($N$ columns)            │ Variable ($0 \le \text{size} \le N$) │
├──────────────────────────┼───────────────────────────────────────────┼──────────────────────────────────────┤
│ Active Slots on Creation │ All $N$ Slots Active (Default SQL NULL)   │ **0 Active Slots (Empty)**           │
├──────────────────────────┼───────────────────────────────────────────┼──────────────────────────────────────┤
│ Size Discovery Mechanism │ Compile-time constant (`constexpr int N`) │ Backwards Tag Scan (`tag >= 0x20`)   │
├──────────────────────────┼───────────────────────────────────────────┼──────────────────────────────────────┤
│ Needs `is_active()`?     │ **No** (Never has inactive slots)         │ **Yes** (Distinguishes active slots) │
├──────────────────────────┼───────────────────────────────────────────┼──────────────────────────────────────┤
│ Needs Initial `memset`?  │ **No** (All elements default constructed) │ **Yes** (Clears stack garbage)       │
├──────────────────────────┼───────────────────────────────────────────┼──────────────────────────────────────┤
│ Container-Level Heap     │ Static Compile-Time ($N \ge 9$)           │ Dynamic Runtime Spill ($\text{sz}>N$)│
└──────────────────────────┴───────────────────────────────────────────┴──────────────────────────────────────┘
```

### 1. Why `SqliteValueVec<N>` Needs `is_active()`
- **Variable-Length on Stack**: `SqliteValueVec<4>` reserves 64 bytes on the stack, but may logically contain 0, 1, 2, 3, or 4 active elements.
- **100% Stack Data Density**: To avoid wasting 4 to 8 bytes on an external `uint32_t m_size` header on the stack, `SqliteValueVec` deduces its size by scanning backwards from slot $N-1$ down to $0$ checking `m_inline[i].is_active()`.
- **Empty Slot Representation**: Unused slots hold `tag == 0x00` ($< \text{0x20}$). The backwards scan immediately stops at the highest slot where `tag >= 0x20`.

### 2. Why `SqliteValueTuple<N>` Never Needs `is_active()`
- **Fixed Arity**: A `SqliteValueTuple<3>` represents a static 3-column Primary Key or fixed record. All 3 slots are **always active** from creation to destruction.
- **Static Size**: Its `size()` method is a `constexpr` constant returning $N$ directly:
  ```cpp
  constexpr int size() const noexcept { return static_cast<int>(N); }
  ```
- **No Inactive Slots**: Default-constructing a tuple initializes all $N$ elements to active `SQLITE_NULL` values (`tag = 0xA0 >= 0x20`). It never needs to scan tags to discover its length.

### 3. Container-Level vs. Element-Level `is_heap()`

`is_heap()` operates at two distinct architectural tiers:

#### Tier A: Container-Level `is_heap()` (Container Storage Location)
- **`SqliteValueVec<N>` (Adaptive Runtime Spill)**:
  - Sizes $\le N$: Operates in-situ on the stack (`is_heap() == false`).
  - Resized $> N$: Spills dynamically to `sqlite3_malloc64` on the heap (`is_heap() == true`).
  - Tested via: `m_heap.tag.is_heap_container(m_heap.ptr)` (checks `raw == 0x00 && ptr != nullptr`).
- **`SqliteValueTuple<N>` (Static Compile-Time Selection)**:
  - $N \le 8$: 100% stack array (`m_values[N]`), 0 heap allocations.
  - $N \ge 9$: 100% heap buffer (`sqlite3_malloc64`), because large tuples exceed stack frame safety limits.

#### Tier B: Element-Level `tag.is_heap()` (Inside Each Individual Column)
Regardless of whether a column is stored inside a Tuple or a Vector, each individual `SqliteValueOwned` element uses bit 4 (`0x10`) of its tag to track where its string/blob payload lives:
- **Short text / blob** ($\le 13$ chars / $\le 14$ bytes): Inline SBO buffer (**`tag.is_heap() == false`**, 0 mallocs).
- **Long text / blob** ($> 13$ chars): `sqlite3_value*` duplicated on heap (**`tag.is_heap() == true`**).

---

## 7. Why `memset` is Required ONLY for `SqliteValueVec<N>`

1. **The Stack Garbage Problem**:
   Uninitialized stack memory contains random residual bytes from previous function calls. If byte offset 15 of an uninitialized stack slot happened to contain a garbage byte $\ge \text{0x20}$ (e.g. `0x7F`, `0x42`, `0xA0`), `SqliteValueVec::size()` would falsely interpret that uninitialized slot as an active element.
2. **The `memset` Solution**:
   `SqliteValueVec<N>` invokes `init_empty()`, which executes `memset(this, 0, sizeof(SqliteValueVec))`. This sets all $N$ tags at byte offset 15 to **`0x00` ($< \text{0x20}$, inactive)** in a single hardware memory burst.
3. **Single-Burst SIMD Lowering**:
   Because `sizeof(SqliteValueVec)` is a `constexpr` constant ($16, 32, 64, 128\text{ B}$), the compiler lowers `memset` to **1–2 SIMD instructions** (`pxor`, `movups`, `vmovups`) executing in **~0.3–0.6 ns (1–2 CPU clock cycles)** without a runtime function call.
4. **Truncation Zeroing**:
   When shrinking (`resize(smaller_count)`), truncated tail elements are destroyed and their slots are wiped with `memset(&m_inline[K], 0, (M - K) * 16)` to guarantee that `is_active()` stops cleanly at the new truncated size.
