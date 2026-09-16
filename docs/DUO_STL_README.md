# DuoSTL User Guide (`include/stl/`)

> **Upstream Repository**: [https://github.com/sqflyer/duostl](https://github.com/sqflyer/duostl)

**DuoSTL** is a zero-dependency, freestanding, dual-ABI container and algorithms library designed specifically for SQLite extensions, high-performance systems, and embedded environments. It provides complete standard library container replacements that operate under `-nostdlib++`, `-fno-exceptions`, and `-fno-rtti` while routing 100% of dynamic allocations through SQLite's internal memory subsystem (`sqlite3_malloc64` / `sqlite3_free`).

---

## 1. Core Design Highlights

- **Freestanding (-nostdlib++)**: Requires zero standard C++ runtime headers (`<vector>`, `<string>`, `<unordered_map>`, `<memory>`, `<new>`). Compiles cleanly in strict freestanding mode.
- **Dual-ABI (Pure C & C++17)**: Every C++ container wraps a standard-layout C mirror struct (`m_inner`) with identical memory layout. C and C++ code can interchange containers with zero translation overhead or copying.
- **100% SQLite Memory Arena Tracking**: All heap allocations route through `sqlite3_malloc64`, `sqlite3_realloc64`, and `sqlite3_free`. Fully tracked by `sqlite3_status64(SQLITE_STATUS_MEMORY_USED)`.
- **Zero Exception Overhead**: Error handling uses return codes, boolean success tags, or `SqliteResult<T>`, eliminating C++ unwinding table bloat.
- **High-Performance Memory Layouts**:
  - **Robin Hood Hash Map**: Open addressing with Distance-to-Initial-Bucket (DIB) probing and backward-shift deletion (no tombstones).
  - **Small Buffer Optimization (SBO)**: `duo::String` stores up to 22 characters inline on the stack with a compact 24-byte struct footprint.
  - **Single-Pointer Footprint**: `duo::HashMap` and `duo::HashSet` occupy exactly 8 bytes on 64-bit platforms.

---

## 2. Header Organization

Include headers from `stl/` based on container requirements:

| Header | Pure C API | C++17 Class Template | Purpose |
| :--- | :--- | :--- | :--- |
| `stl/duo_alloc.h` / `.hpp` | `duo_alloc_t`, `DUO_MALLOC` | Freestanding traits, `construct_at` | SQLite memory arena mapping & freestanding type traits |
| `stl/duo_linear.h` / `.hpp` | `duo_vec_t`, `duo_string_t`, `duo_span_t` | `duo::Vector<T>`, `duo::String`, `duo::Span<T>`, `duo::StringView` | Dynamic vectors, SBO strings, non-owning views |
| `stl/duo_hash.h` / `.hpp` | `duo_hashmap_t`, `duo_hashset_t` | `duo::HashMap<K, V>`, `duo::HashSet<T>` | High-performance $\mathcal{O}(1)$ Robin Hood hash maps and sets |
| `stl/duo_bit.h` / `.hpp` | `duo_bitvec_t`, `duo_bitspan_t` | `duo::BitVector`, `duo::BitSpan` | Compact bit vectors and bit manipulation primitives |

All C++ headers automatically include their corresponding C headers inside `extern "C"` blocks.

---

## 3. Quickstart Examples

### 3.1 `duo::HashMap` (C++17)

```cpp
#include "stl/duo_hash.hpp"
#include "stl/duo_linear.hpp"

void example_hash_map() {
    // 1. Pointer Hash Map (Key: sqlite3*, Value: SessionData*)
    duo::HashMap<sqlite3*, int> conn_counts;
    conn_counts.insert(db, 42);

    int* val = conn_counts.get(db);
    if (val) {
        *val += 1;
    }

    // 2. String Hash Map (Key: duo::String, Value: Entry*)
    duo::HashMap<duo::String, const char*> config;
    config.insert(duo::String("cache_size"), "64MB");
    config.insert(duo::String("wal_mode"), "normal");

    // Structured binding iteration
    for (auto [key, value] : config) {
        printf("Key: %s -> %s\n", key.c_str(), value);
    }
}
```

### 3.2 `duo_hashmap_t` (Pure C)

```c
#include "stl/duo_hash.h"
#include "stl/duo_linear.h"

void example_c_hash_map() {
    // Create an O(1) hash map: key = sqlite3*, val = int
    duo_hashmap_t *map = duo_hashmap_new(
        sizeof(sqlite3*), sizeof(int), 
        16, 0, 0, 
        duo_hash_ptr, duo_compare_ptr, 
        NULL, NULL, NULL
    );

    sqlite3 *db_key = (sqlite3*)0x1234;
    int count = 100;
    duo_hashmap_set(map, &db_key, &count);

    int *found = (int*)duo_hashmap_get(map, &db_key);
    if (found) {
        printf("Found: %d\n", *found);
    }

    duo_hashmap_free(map);
}
```

### 3.3 `duo::String` & `duo::StringView` (C++17)

```cpp
#include "stl/duo_linear.hpp"

void example_strings() {
    // 1. Inline SBO Mode (Length <= 22 characters: Zero Heap Allocations)
    duo::String inline_str("hello_world");
    assert(inline_str.is_sbo());
    assert(inline_str.size() == 11);

    // 2. Heap Promotion (Length > 22 characters: Uses sqlite3_malloc64)
    duo::String heap_str("file:mydb.sqlite?mode=memory&cache=shared");
    assert(!heap_str.is_sbo());

    // 3. StringView Slicing (Zero Copy)
    duo::StringView view = heap_str.as_view().substr(0, 4);
    assert(view == "file");

    // 4. Mutation & Appending
    inline_str += "_extension";
    printf("Result: %s\n", inline_str.c_str());
}
```

### 3.4 `duo::Vector` (C++17)

```cpp
#include "stl/duo_linear.hpp"

void example_vector() {
    duo::Vector<int> vec;
    vec.reserve(64);

    for (int i = 0; i < 100; ++i) {
        vec.push_back(i * 10);
    }

    for (size_t i = 0; i < vec.size(); ++i) {
        printf("[%zu] = %d\n", i, vec[i]);
    }

    // Range-based for loop
    for (int num : vec) {
        (void)num;
    }
}
```

---

## 4. SQLite State Registry Integration

DuoSTL is the foundational container engine for `sqlite-ext-core`'s state registries:

1. **`SqliteConnState<T>`** (`include/sqlite3_conn_state.hpp`):
   - Uses `duo::HashMap<sqlite3*, Entry*>` for lock-free, per-connection unique state lookup.
2. **`SqliteExtState<T>`** (`include/sqlite3_ext_state.hpp`):
   - Uses `duo::HashMap<duo::String, Entry*>` for per-database shared state registries across connections.
3. **Pure C Registries** (`include/sqlite3_conn_state.h`, `include/sqlite3_ext_state.h`):
   - Backed by `duo_hashmap_t*` with direct `duo_string_t` values as collision-proof path keys.

---

## 5. Memory Safety & Leak Verification

All DuoSTL containers are fully integrated with AddressSanitizer (ASan) and LeakSanitizer (LSan):

- When compiling with `-fsanitize=address,leak`, DuoSTL container allocations are verified byte-for-byte on destruction.
- Empty hash maps and string heap buffers automatically deallocate memory upon teardown, guaranteeing **0 leaked bytes**.

Run memory verification test suite:
```bash
# MSYS2 Clang64
cd tests/cpp_duo && make clean && make test

# Linux GCC with ASan + LSan
wsl bash -lc "cd /mnt/c/msys64/home/dilipvamsi/works/repos/sqlite-ext-core/tests/cpp_duo && make clean && make test"
```
