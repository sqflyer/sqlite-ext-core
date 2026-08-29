# 16-Byte Small Buffer Optimized Row Key Architecture (`sqlite3_row_key.hpp`)

This document details the internal systems architecture, binary layout, 16-byte Small Buffer Optimization (SBO) state machine, L1 cache line density calculations, and transparent relational lookup engines implemented in [`include/sqlite3_row_key.hpp`](../include/sqlite3_row_key.hpp).

---

## 1. Executive Summary & Systems Design Goals

Database index nodes (B-Tree internal/leaf nodes, Swiss Table buckets, and MemKV cache rings) spend a major portion of CPU time comparing keys and resolving cache misses. 

Standard dynamic row representations (e.g. `std::vector<SqliteValueOwned>` or dynamic structs) have a **24-to-32 byte control structure** plus an **indirection pointer to a separate heap allocation**. Traversing index trees with dynamic rows causes severe heap memory fragmentation and frequent L1/L2 cache misses.

`SqliteRowKeyOwned` was engineered to satisfy five strict systems constraints:

1. **Exact 16-Byte Footprint**: Guaranteed `sizeof(SqliteRowKeyOwned) == 16` across all compile-time and runtime configurations.
2. **L1 Cache Line Alignment**: Fits **exactly 4 complete keys in a single 64-byte L1 CPU cache line** ($4 \times 16\text{B} = 64\text{B}$).
3. **100% In-Situ for 1-Column Keys**: Stored with **0 heap allocations and 0 pointer hops** for single-column primary keys ($N=1$, representing over 95% of real-world database tables).
4. **Dual Representation Overlapping Union**: Reuses the exact same 16-byte memory footprint for composite keys ($N \ge 2$) via a compact dynamic heap descriptor.
5. **Zero-Allocation Heterogeneous Map Queries**: Enables direct container lookups using raw primitive types (`int`, `double`, `const char*`, `SqliteStringView`) without constructing intermediate key objects.

---

## 2. Binary Layout & The 16-Byte Overlapping Union

`SqliteRowKeyOwned` achieves its dual-mode capabilities through an overlapping 16-byte union multiplexed by a shared control tag byte located at **Offset 15 (Byte 15)**:

```cpp
class SqliteRowKeyOwned {
private:
    struct HeapRep {
        SqliteValueOwned*   ptr;      // 8 Bytes (Offset 0..7)
        uint32_t            size;     // 4 Bytes (Offset 8..11)
        uint16_t            capacity; // 2 Bytes (Offset 12..13)
        uint8_t             reserved; // 1 Byte  (Offset 14)
        SqliteOwnedValueTag tag;      // 1 Byte  (Offset 15, Shared Tag Byte!)
    };
    static_assert(sizeof(HeapRep) == 16, "HeapRep must be exactly 16 bytes");

    union {
        SqliteValueOwned m_single; // Mode A: In-Situ Value (Tag at Offset 15)
        HeapRep          m_heap;   // Mode B: Array Descriptor (Tag at Offset 15)
        uint64_t         m_align;  // Forces 8-byte alignment
    };
};
```

---

### 2.1 Mode A: Single-Column In-Situ Representation ($N = 1$)

When $N=1$, the entire 16 bytes are occupied directly by an in-situ `SqliteValueOwned`:

```
Byte Offset:
 0                   4                   8                   12              14  15
┌───────────────────┬───────────────────┬───────────────────┬───────────────┬───┬───┐
│              Primary 64-bit Payload   │  Secondary Length / String Cap   │Sub│Tag│
│  (int64 / double / inline text / blob)│       (For SBO Text / Blob)       │typ│   │
└───────────────────┴───────────────────┴───────────────────┴───────────────┴───┴───┘
 ◀─────────────────────────────── In-Situ Value (15 Bytes) ────────────────────────► ◀1B▶
                                                                                      │
                                             Tag Byte != 0x00 (is_row_key() == false) ┘
```

- **Integer / Real**: Stored in 8-byte primary payload (0 heap allocations).
- **Small Text**: Strings up to 13 characters stored in-situ (0 heap allocations).
- **Small Blob**: Blobs up to 14 bytes stored in-situ (0 heap allocations).
- **Tag Byte (Byte 15)**: Holds the SQLite datatype code (`SQLITE_INTEGER`, `SQLITE_TEXT`, etc.). Because scalar tag values are non-zero with `FLAG_ROW = 0`, `is_row_key()` evaluates to `false` in a single CPU register test.

---

### 2.2 Mode B: Multi-Column Composite Representation ($N \ge 2$ or $N = 0$)

When $N \ge 2$ (or $N = 0$ for an empty key), the same 16 bytes are interpreted as a dynamic array descriptor (`HeapRep`):

```
Byte Offset:
 0                   4                   8                   12        14    15
┌───────────────────────────────────────┬───────────────────┬─────────┬─────┬───┐
│     SqliteValueOwned* ptr (8 Bytes)   │  size (4 Bytes)   │cap (2B) │res  │Tag│
│   (Points to contiguous heap buffer)  │  (Column Count)   │         │ (1B)│0x0│
└───────────────────────────────────────┴───────────────────┴─────────┴─────┴───┘
 ◀────────────── Pointer ──────────────► ◀───── uint32_t ──► ◀ uint16 ─► ◀1B─► ◀1B▶
                                                                                │
                                             Tag Byte == 0x00 (is_row_key() == true) ───┘
```

- **`ptr` (8 Bytes)**: Points to a contiguous `sqlite3_malloc64` array of `SqliteValueOwned`.
- **`size` (4 Bytes)**: Supports up to $2^{32}-1$ composite columns (typically 2 to 8 columns).
- **`capacity` (2 Bytes)**: Tracks allocated array capacity for amortized dynamic growth.
- **`tag` (Byte 15)**: Initialized to `0x00` (`tag.set_as_row_key()`). The condition `tag.is_row_key()` evaluates to `true`, distinguishing composite mode from in-situ scalar mode.

---

## 3. SBO State Machine Transitions

`SqliteRowKeyOwned` automatically transitions between Mode A (In-situ) and Mode B (Heap) during mutation or resizing:

```
                      ┌───────────────────────────────┐
                      │          Empty Key            │
                      │     (size = 0, Mode B)        │
                      └──────────────┬────────────────┘
                                     │
           ┌─────────────────────────┴─────────────────────────┐
           ▼                                                   ▼
┌───────────────────────────────┐                   ┌───────────────────────────────┐
│    Single Key (Mode A)        │   resize(N >= 2)  │    Composite Key (Mode B)     │
│  - In-situ SqliteValueOwned   ├──────────────────►│  - HeapRep descriptor         │
│  - 0 Heap Allocations         │◄──────────────────┤  - Contiguous heap array      │
│  - is_row_key() == false      │   resize(N == 1)  │  - is_row_key() == true       │
└───────────────────────────────┘                   └───────────────────────────────┘
```

### Transition Logic in `resize()`:

1. **Expanding $1 \to N$ ($N \ge 2$)**:
   - Moves the in-situ `m_single` to a temporary variable.
   - Invokes destructor `m_single.~SqliteValueOwned()`.
   - Initializes `m_heap` descriptor with capacity $N$.
   - Adopts the original single value at index `m_heap.ptr[0]`, initializing remaining columns to `SQLITE_NULL`.
2. **Shrinking $N \to 1$**:
   - Moves `m_heap.ptr[0]` to a temporary variable.
   - Destroys and frees the heap buffer via `sqlite_destroy_n` and `sqlite_delete_array`.
   - In-place constructs `m_single` from the temporary value via placement new.
3. **Clearing to 0**:
   - Destroys active payload (freeing heap if Mode B) and resets `m_heap` to null/empty state.

---

## 4. L1 Cache Line Density & Traversal Performance

Modern CPU memory hierarchies transfer data between DRAM and L1 cache in **64-byte cache lines**.

```
┌───────────────────────────────────────────────────────────────────────────────┐
│                       64-BYTE L1 CPU CACHE LINE                               │
│                                                                               │
│ ┌────────────────┬────────────────┬────────────────┬────────────────────────┐ │
│ │ Key 0 (16 B)   │ Key 1 (16 B)   │ Key 2 (16 B)   │ Key 3 (16 B)           │ │
│ └────────────────┴────────────────┴────────────────┴────────────────────────┘ │
└───────────────────────────────────────────────────────────────────────────────┘
  1 Cache Miss = 4 COMPLETE PRIMARY KEYS Loaded Simultaneously into L1 Cache!
```

### Comparative Analysis:

| Implementation | Size per Key | Keys per 64B Cache Line | Cache Line Utilization |
| :--- | :---: | :---: | :---: |
| **`SqliteRowKeyOwned`** | **16 Bytes** | **4.0 Keys** | **100% (Dense)** |
| `std::vector<SqliteValueOwned>` | 24B + Heap Pointer | 0.8 Keys (with indirection) | 33% + Indirection Stalls |
| Raw SQLite `sqlite3_value*` | 8B Pointer + 64B Struct | 0.9 Keys | High Indirection Latency |

---

## 5. Lexicographical Relational Comparison Engine

`SqliteRowKeyOwned` implements all 6 relational operators ($==, \ne, <, \le, >, \ge$) across all permutations of keys, row spans, strings, blobs, and native primitive types.

### 5.1 Symmetric Macro Code Generation

To eliminate boilerplate and ensure complete relational consistency, operators are generated via C++ preprocessor macros:

- `SQLITE_DERIVE_KEY_SCALAR_RELATIONAL_OPS(Type)`: Generates member operators against scalar types (`int`, `sqlite3_int64`, `double`, `bool`) and symmetric non-member reverse operators (`42 == key`, `3.14 < key`).
- `SQLITE_DERIVE_KEY_CSTR_RELATIONAL_OPS`: Generates non-member C-string reverse operators (`"admin" == key`, `"guest" < key`) avoiding duplicate `const` compiler warnings.

### 5.2 Single vs. Composite Prefix Ordering Semantics

Standard tuple mathematics dictate that a single scalar $(X)$ represents a 1-tuple $(X)$. When compared against a composite tuple $(X, Y)$:

1. **Prefix Match**: The first column $(X)$ matches. Because $(X)$ has length 1 and $(X, Y)$ has length 2, the shorter tuple is strictly smaller:
   $$(10) < (10, 20) \implies \mathbf{true} \quad (\text{Identical to } \text{"a"} < \text{"ab"})$$
2. **Prefix Mismatch**: If the first column differs, the first column decides the ordering:
   $$(15) > (10, 20) \implies \mathbf{true} \quad (\text{Because } 15 > 10)$$

---

## 6. Transparent Functors for STL & B-Trees

To enable **zero-allocation heterogeneous lookups** in associative containers (`std::map`, `std::unordered_map`, Swiss Tables), `sqlite3_row_key.hpp` defines transparent functors with `using is_transparent = void;` synthesized via universal macros:

```cpp
struct SqliteRowKeyHash {
    using is_transparent = void;

    inline size_t operator()(const SqliteRowKeyOwned& k) const noexcept { return static_cast<size_t>(k.hash()); }
    SQLITE_DERIVE_TRANSPARENT_ROW_HASH_OVERLOADS
};

SQLITE_DERIVE_TRANSPARENT_EQUAL(SqliteRowKeyEqual)
SQLITE_DERIVE_TRANSPARENT_LESS(SqliteRowKeyLess)
```

### Zero-Allocation Query Execution Path:

```
btree.find(42)
  │
  ├──► Compiler checks SqliteRowKeyLess::is_transparent
  ├──► Invokes operator()(const SqliteRowKeyOwned& node_key, int lookup_val)
  ├──► Compares node_key[0].as_int64() directly against 42 in registers
  └──► ZERO heap allocations, ZERO temporary objects constructed!
```

---

## 7. Unified MurmurHash2 Composite Combining (`SQLITE_DERIVE_ARRAY_HASH`)

When computing the hash of an array/tabular container or composite key, `hash()` is synthesized uniformly via `SQLITE_DERIVE_ARRAY_HASH`:

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

### Execution Invariants:
1. **Mode A ($N=1$ In-Situ)**: `sz == 1`, directly returning `(*this)[0].hash()` (`m_single.hash()`) with **0 loop overhead and 0 mallocs**.
2. **Mode B ($N \ge 2$ Composite)**: Sequentially folds column hashes using `SqliteHashUtil::combine()` to guarantee strong avalanche mixing without bit cancellation.
3. **Empty Keys ($N=0$)**: Returns `SqliteHashUtil::DEFAULT_SEED` deterministically.

---

## 8. Macro Synthesized Typed Extraction (`SQLITE_DERIVE_ARRAY_ACCESSORS`)

`SqliteRowKeyOwned` implements the universal `SQLITE_DERIVE_ARRAY_ACCESSORS` macro interface:
- **Direct Column Accessors**: Exposes inlined `as_int64(i = 0)`, `as_int(i = 0)`, `as_double(i = 0)`, `as_text(i = 0)`, `as_blob(i = 0)`, `as_bool(i = 0)`, `is_null(i = 0)`, `type(i = 0)`, and `subtype(i = 0)`.
- **Default Parameter (`index = 0`)**: Allows single-column primary keys to omit the column index (e.g. `k.as_int64()`, `k.as_text()`).
- **Orthogonal Hashing**: Paired with `SQLITE_DERIVE_ARRAY_HASH` for uniform MurmurHash2 digests across all containers.
- **Zero Indirection**: Mode A directly delegates to in-situ scalar methods; Mode B indexes into the heap buffer with no temporary object creation.
