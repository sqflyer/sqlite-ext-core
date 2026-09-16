# DuoSTL Technical Architecture & Deep Internals

> **Upstream Repository**: [https://github.com/sqflyer/duostl](https://github.com/sqflyer/duostl)

This document details the architectural design, memory layouts, algorithmic invariants, and ABI compatibility models of **DuoSTL** (`include/stl/`).

---

## 1. Architectural Philosophy & Guarantees

SQLite extensions require deterministic memory management, zero external dynamic runtime dependencies, and instant interoperability between Pure C C11 and C++17 translation units. Standard C++ STL containers (`std::string`, `std::vector`, `std::unordered_map`) fail these requirements because they:
1. Mandate dynamic C++ runtime libraries (`libc++.so` / `libstdc++.so`).
2. Allocate via global `::operator new` and `malloc`, bypassing SQLite's `sqlite3_malloc64` tracking arena.
3. Possess non-standardized ABIs that cannot be passed safely across C/C++ boundaries.
4. Use exception-based error reporting (`std::bad_alloc`), adding stack unwinding overhead.

DuoSTL eliminates these limitations through five architectural guarantees:

```text
+-----------------------------------------------------------------------------+
|                          DuoSTL Architectural Pillars                       |
+-----------------------------------------------------------------------------+
|  1. Dual-ABI Binary Compatibility  : Bit-for-bit equivalence between C & C++|
|  2. Strict Freestanding Execution  : -nostdlib++, -fno-exceptions, -fno-rtti|
|  3. 100% SQLite Memory Arena       : Direct route to sqlite3_malloc64/free  |
|  4. Low-Variance Robin Hood Hashing: DIB probing & backward-shift deletion  |
|  5. Zero-Allocation SBO Strings    : Inline 22-char buffers in 24-byte cells|
+-----------------------------------------------------------------------------+
```

---

## 2. Dual-ABI Architecture & Zero-Cost C FFI

Every C++ container in DuoSTL wraps a standard-layout Pure C mirror struct as a single public member named `m_inner`.

### 2.1 The Standard-Layout Invariant

```cpp
namespace duo {
    template <typename T>
    class Vector {
    public:
        using c_type = duo_vec_t;
        c_type m_inner; // Exactly 24 bytes (data, size, capacity)
        ...
    };

    class String {
    public:
        using c_type = duo_string_t;
        c_type m_inner; // Exactly 24 bytes (SBO inline buffer or heap triple)
        ...
    };

    template <typename Key, typename Value>
    class HashMap {
    public:
        using c_type = c_hashmap_t<Key, Value>;
        c_type m_inner; // Exactly 8 bytes (single pointer to duo_hashmap_t)
        ...
    };
}
```

Because `m_inner` is the sole data member, DuoSTL classes have:
- `is_standard_layout<Container>::value == true`.
- Exact sizeof equivalence: `sizeof(duo::Vector<T>) == sizeof(duo_vec_t)`.
- Identical pointer alignment: `reinterpret_cast<duo_vec_t*>(&vec) == &vec.m_inner`.

### 2.2 Dual-ABI Foreign Function Interface (FFI)

The `DUO_CXX_FFI_OPS(InnerMember)` macro injects three zero-cost FFI pathways into every container:

```text
+-----------------------+                    +-----------------------+
|  C++17 duo::Vector<T> |                    |   Pure C duo_vec_t    |
+-----------------------+                    +-----------------------+
|      m_inner          |                    |  data | size | cap    |
+-----------------------+                    +-----------------------+
           |                                             ^
           |-- 1. Implicit Conversion: (duo_vec_t)vec --|
           |-- 2. Direct Member Pass:  vec.m_inner ------|
           +-- 3. Raw Pointer Pass:    &vec.m_inner -----+
```

This enables seamless zero-copy handover: a C++ extension function can construct a `duo::Vector<int>`, pass its address to a Pure C utility function taking `duo_vec_t*`, mutate it, and return to C++ with zero wrapping or marshalling overhead.

---

## 3. Memory Subsystem Integration (`duo_alloc.h` / `.hpp`)

DuoSTL completely decouples itself from standard dynamic memory and binds directly to SQLite's memory arena:

```c
#if defined(SQLITE_CORE) || defined(SQLITE_ENABLE_MEMSYS5) || defined(DUO_USE_SQLITE_ALLOC)
    #define DUO_MALLOC(sz)        sqlite3_malloc64((sqlite3_uint64)(sz))
    #define DUO_FREE(ptr)         sqlite3_free(ptr)
    #define DUO_REALLOC(ptr, sz)  sqlite3_realloc64((ptr), (sqlite3_uint64)(sz))
#endif
```

### 3.1 Freestanding In-Place Construction & Destruction

Under `-nostdlib++`, the standard `<new>` header and `std::construct_at` are absent. `stl/duo_alloc.hpp` provides freestanding placement wrappers:

```cpp
template <typename T, typename... Args>
inline T* construct_at(T* ptr, Args&&... args) noexcept {
    return ::new (static_cast<void*>(ptr)) T(static_cast<Args&&>(args)...);
}

template <typename T>
inline void destroy_at(T* ptr) noexcept {
    if constexpr (!is_trivially_destructible<T>::value) {
        ptr->~T();
    }
}
```

- When `is_trivially_destructible<T>` is true, `destroy_at` compiles down to a no-op, eliminating redundant destructor loops on primitive and standard-layout types.

---

## 4. Small Buffer Optimization (SBO) String (`duo_string_t` / `duo::String`)

String keys (such as SQLite database paths) in state registries are predominantly short (e.g. `/var/data.db`, `:memory:0x1234`). Standard dynamic allocation on every key lookup creates severe heap fragmentation and lock contention.

`duo_string_t` provides a Small Buffer Optimized string with an exact 24-byte footprint:

### 4.1 Memory Layout

```text
1. Inline SBO Mode (Length <= 22 bytes):
+-------------------------------------------------------------+----+----+
|                  inline_data (22 bytes)                     |len | 0  |
+-------------------------------------------------------------+----+----+
 0                                                            21   22   23

2. Dynamic Heap Mode (Length > 22 bytes):
+--------------------+--------------------+--------------------+----+----+
|   m_data (8 bytes) |   m_size (8 bytes) |   m_cap (6 bytes)  |unused| 1|
+--------------------+--------------------+--------------------+----+----+
 0                   8                    16                   22   23   24
```

### 4.2 Encoding Invariants
- **Discriminant Bit**: The highest bit of byte 23 distinguishes inline SBO mode (`0`) from heap mode (`1`).
- **Null-Termination Invariant**: Both inline and heap representations are guaranteed to be null-terminated at all times. `c_str()` and `data()` return in $\mathcal{O}(1)$ time without checking allocation status.
- **Dynamic Demotion**: Calling `shrink_to_fit()` on a heap string whose length has dropped to $\le 22$ bytes automatically releases the dynamic buffer via `DUO_FREE` and demotes the string back to inline SBO mode.

---

## 5. Robin Hood Hash Map Architecture (`duo_hashmap_t` / `duo::HashMap`)

`duo::HashMap` implements an open-addressed hash table utilizing **Robin Hood hashing** with **Distance-to-Initial-Bucket (DIB)** probing and **backward-shift deletion**.

### 5.1 The Robin Hood Invariant

In standard open addressing with linear probing, clustering causes lookup variance to degrade sharply ($O(N)$ worst-case). Robin Hood hashing bounds probe sequence lengths by equalizing search distances:

> **The Invariant**: If during insertion, the key being inserted has a Distance-to-Initial-Bucket ($DIB_{incoming}$) strictly greater than the $DIB$ of the element occupying the current bucket ($DIB_{resident}$), the incoming element *steals* the bucket. The displaced resident element is then probed forward into subsequent buckets.

```text
Bucket Sequence:
[ Index 4 ] : Key A (DIB = 0)
[ Index 5 ] : Key B (DIB = 1)
[ Index 6 ] : Key C (DIB = 2)

Inserting Key X (Home Bucket = 4, Current DIB = 2 at Index 6):
  - At Index 6: DIB(X) == 2, DIB(C) == 2 -> Probe next.
  - At Index 7: Empty -> Insert Key X (DIB = 3).
```

### 5.2 Backward-Shift Deletion (Tombstone-Free)

Standard hash tables mark deleted buckets with tombstones (`TOMBSTONE`), which pollutes probe chains and forces periodic table rebuilding.

DuoSTL employs **backward-shift deletion**:
1. When key at index $i$ is deleted, its bucket is cleared.
2. The algorithm inspects the succeeding bucket ($i + 1$).
3. If the element at $i + 1$ has $DIB > 0$, it is shifted backward into bucket $i$, and its $DIB$ is decremented by 1.
4. This shift cascades until an empty bucket or an element with $DIB == 0$ is encountered.

```text
Before Deletion of Bucket 5:
Index:   [ 4 ]     [ 5 ]     [ 6 ]     [ 7 ]     [ 8 ]
Key:     Key A     Key B     Key C     Key D     EMPTY
DIB:       0         1         2         1         -

Delete Key B at Index 5:
Shift Key C: [ 6 ] -> [ 5 ], DIB(C) = 2 - 1 = 1
Shift Key D: [ 7 ] -> [ 6 ], DIB(D) = 1 - 1 = 0
Clear:       [ 7 ] = EMPTY

Result:
Index:   [ 4 ]     [ 5 ]     [ 6 ]     [ 7 ]     [ 8 ]
Key:     Key A     Key C     Key D     EMPTY     EMPTY
DIB:       0         1         0         -         -
```

**Benefits**:
- **Zero tombstones**: Lookup chains stay short and compact.
- **Cache-locality**: Shifts operate linearly along contiguous cache lines.
- **Guaranteed Teardown**: Deleting the final element returns count to $0$, allowing immediate release of the bucket array via `duo_hashmap_free`.

### 5.3 Dual Key & Value Destructors

To support complex non-trivial C++ types without heap leakage, `duo_hashmap_t` supports independent key and value destructor trampolines:

```c
typedef void (*duo_element_destructor_t)(void *element);
```

When a bucket is deleted via backward-shift or when the entire table is destroyed, `key_destructor` and `val_destructor` are invoked independently on the elements, guaranteeing safe cleanup of embedded resources.

---

## 6. Macro-Based Algorithm Injection

Rather than relying on C++ class inheritance (which introduces base-class padding, virtual table pointer overhead, and ABI divergence from C structs), DuoSTL uses macro code generation:

| Macro | Injected Functionality | ABI Effect |
| :--- | :--- | :--- |
| `DUO_CXX_FFI_OPS(m_inner)` | `c_ptr()`, `c_val()`, implicit conversion operators | 0 bytes added, preserves standard layout |
| `DUO_CXX_BULK_OPS(Type, ...)` | `copy_from`, `copy_to`, `move_from`, `move_to`, `fill` | Hardware SIMD acceleration via `memcpy`/`memmove` |
| `DUO_CXX_STANDARD_ACCESSORS` | `begin()`, `end()`, `operator[]`, `front()`, `back()` | Full STL compliance without virtual methods |
| `DUO_CXX_DERIVE_HASH` | 64-bit member `.hash(seed0, seed1)` via xxHash3 | Inlines hash calculation directly onto the struct |

---

## 7. Comparative Metrics & Footprint

| Metric | `std::unordered_map` | `stb_ds.h` (Legacy) | `duo::HashMap` (DuoSTL) |
| :--- | :--- | :--- | :--- |
| **Struct Size (64-bit)** | 48–56 bytes | 8 bytes (raw pointer) | **8 bytes** (single pointer) |
| **Bucket Organization** | Separate chaining (node based) | Open addressing | **Open addressing (Robin Hood)** |
| **Node Overhead** | 24–32 bytes per entry | 0 bytes | **0 bytes** (flat array) |
| **Probe Strategy** | Linked list pointer chasing | Linear probing | **DIB-bounded Robin Hood** |
| **Tombstones on Delete** | N/A (unlinks node) | Tombstones degrade probes | **None** (backward-shift deletion) |
| **Freestanding (-nostdlib++)** | No (requires runtime) | Yes | **Yes** (100% freestanding) |
| **SQLite Malloc Tracking** | No (global new/malloc) | Partial (via macros) | **100% native** (`duo_alloc.h`) |
| **Dual-ABI (C & C++)** | C++ only | Pure C only | **Dual-ABI** (shared layout) |
