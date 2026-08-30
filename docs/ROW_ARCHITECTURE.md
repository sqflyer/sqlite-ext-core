# C++ Row Types Architecture (`sqlite3_row.hpp`)

This document provides an exhaustive systems-level architectural analysis of `sqlite3_row.hpp`, detailing its **multi-source tagged multiplexing engine**, **24-byte universal row view (`SqliteRowView`)**, **16-byte span representation (`SqliteRowOwnedWrapper`)**, **assembly-level execution characteristics**, and **freestanding memory guarantees**.

> **API & Usage Guide**: For practical usage tutorials, code examples, and the public API reference, see [`docs/ROW_README.md`](ROW_README.md).  
> **Value Containers Architecture**: For owning multi-column primary key tuples and adaptive SBO vectors, see [`docs/VALUE_CONTAINERS_ARCHITECTURE.md`](VALUE_CONTAINERS_ARCHITECTURE.md).

---

## 1. Architectural Motivation: Tabular Row Management in SQLite Extensions

SQLite extensions and virtual tables frequently interact with tabular records across three distinct lifecycles:

1. **VDBE Evaluation Register Windows (`sqlite3_stmt*`)**: Prepared statement execution produces rows where column values are held in SQLite's internal `struct Mem` array. Extracting these columns using traditional wrappers often triggers unnecessary buffer copying and dynamic heap allocations.
2. **UDF Argument Vectors (`sqlite3_value** argv`)**: Scalar functions, aggregate step routines, and virtual table filter/update methods receive transient pointer arrays that require unified bounds-checked inspection.
3. **Owned Contiguous Buffers (Spans)**: High-throughput virtual table implementations (e.g. in-memory key-value stores, LRU caches, ring buffers) require structured row containers that store records with minimal memory bloat, zero fragmentation, and optimal cache locality.

`sqlite3_row.hpp` organizes these responsibilities into **Zero-Allocation Multi-Source Views** and **16-Byte Spans**:

```
┌───────────────────────────────────────────────────────────────────────────────────────┐
│                                   SqliteRowView                                       │
│   - Zero dynamic allocations (stack/register resident)                                │
│   - Multi-source tagged union multiplexing:                                           │
│       • Prepared Statements (sqlite3_stmt*)                                           │
│       • UDF / Aggregate Argv (sqlite3_value**)                                        │
│       • Transient View Arrays (const SqliteValueView*)                                │
│   [24 Bytes: 16B Tagged Union + 4B Col Count + 1B Source Tag + 3B Pad]                │
└───────────────────────────────────────────────────────────────────────────────────────┘
                                           ▲
                        .to_vec() / .to_tuple() extraction
                                           │
┌───────────────────────────────────────────────────────────────────────────────────────┐
│                        SqliteRowOwnedWrapper (Span)                                   │
│   - 16-byte non-owning span over contiguous SqliteValueOwned arrays (2 CPU Registers) │
│   - Wraps: SqliteValueTuple<N>, SqliteValueVec<N>, C-arrays, and single values        │
└───────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Universal 24-Byte Multi-Source Row View (`SqliteRowView`)

`SqliteRowView` multiplexes four backing memory models without virtual method tables (`vtable`), function pointers, or heap allocation:

```
Byte Offset:  0                       8                       16      20   21  23
              ┌───────────────────────┬───────────────────────┬───────┬────┬───┐
              │ m_stmt / m_argv /     │ (union storage)       │m_count│src │pad│
              │ m_view_array          │                       │[4B]   │[1B]│[3]│
              │ [8 Bytes Pointer]     │ [8 Bytes]             │       │    │   │
              └───────────────────────┴───────────────────────┴───────┴────┴───┘
```

### Source Discriminator Tag (`m_source`)
- `SQLITE_ROW_SOURCE_STMT (0)`: Direct column extraction from active prepared statements.
- `SQLITE_ROW_SOURCE_ARGV (1)`: Direct pointer access into SQLite UDF argument arrays (`sqlite3_value**`).
- `SQLITE_ROW_SOURCE_VIEW_ARRAY (2)`: Contiguous in-memory `SqliteValueView*` array.
- `SQLITE_ROW_SOURCE_EMPTY (3)`: Null / empty row view.

---

## 3. 16-Byte Span: `SqliteRowOwnedWrapper`

`SqliteRowOwnedWrapper` encapsulates a non-owning span over contiguous `SqliteValueOwned` memory:

```cpp
class SqliteRowOwnedWrapper {
    SqliteValueOwned* m_data; // 8 Bytes
    int               m_len;  // 4 Bytes (+ 4 Bytes padding = 16 Bytes total)
};
```

### Assembly Execution:
Because `sizeof(SqliteRowOwnedWrapper) == 16` bytes, modern x86-64 and ARM64 ABIs return and pass it **directly in 2 CPU registers (`rax`, `rdx` / `x0`, `x1`)** with zero stack spill.

---

## 4. Stack-Allocated Scope Dispatcher (`withSqliteRowOwned`)

When converting a dynamic runtime `SqliteRowView` into an owned stack container, `withSqliteRowOwned` executes compile-time dispatching across sizes 1..8:

```cpp
template <typename Fn>
void withSqliteRowOwned(const SqliteRowView& row, Fn&& fn) {
    switch (row.size()) {
        case 1: { SqliteValueTuple<1> t(row); fn(t.view()); } break;
        case 2: { SqliteValueTuple<2> t(row); fn(t.view()); } break;
        case 3: { SqliteValueTuple<3> t(row); fn(t.view()); } break;
        case 4: { SqliteValueTuple<4> t(row); fn(t.view()); } break;
        case 5: { SqliteValueTuple<5> t(row); fn(t.view()); } break;
        case 6: { SqliteValueTuple<6> t(row); fn(t.view()); } break;
        case 7: { SqliteValueTuple<7> t(row); fn(t.view()); } break;
        case 8: { SqliteValueTuple<8> t(row); fn(t.view()); } break;
        default: {
            SqliteValueVec<9> vec(row.size());
            // Populate and dispatch...
            fn(vec.view());
        } break;
    }
}
```

This guarantees **zero heap allocations** for all queries and records with $\le 8$ columns.
