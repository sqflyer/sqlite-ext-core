# C++ Row Types Architecture (`sqlite3_row.hpp`)

This document provides an exhaustive systems-level architectural analysis of `sqlite3_row.hpp`, detailing its **multi-source tagged multiplexing engine**, **16-byte universal row view (`SqliteRowView`)**, **16-byte owned view (`SqliteRowOwnedView`)**, **16-byte span representation (`SqliteRowOwnedWrapper`)**, **standard `std::array` compliance**, **`sqlite_reverse_iterator` proxy mechanics**, **assembly-level execution characteristics**, and **freestanding memory guarantees**.

> **API & Usage Guide**: For practical usage tutorials, code examples, and the public API reference, see [`docs/ROW_README.md`](ROW_README.md).  
> **Value Containers Architecture**: For owning multi-column primary key tuples and adaptive SBO vectors, see [`docs/VALUE_CONTAINERS_ARCHITECTURE.md`](VALUE_CONTAINERS_ARCHITECTURE.md).

---

## 1. Architectural Motivation: Tabular Row Management in SQLite Extensions

SQLite extensions and virtual tables frequently interact with tabular records across three distinct lifecycles:

1. **VDBE Evaluation Register Windows (`sqlite3_stmt*`)**: Prepared statement execution produces rows where column values are held in SQLite's internal `struct Mem` array. Extracting these columns using traditional wrappers often triggers unnecessary buffer copying and dynamic heap allocations.
2. **UDF Argument Vectors (`sqlite3_value** argv`)**: Scalar functions, aggregate step routines, and virtual table filter/update methods receive transient pointer arrays that require unified bounds-checked inspection, standard iterators, and zero allocations.
3. **Owned Contiguous Buffers (Spans)**: High-throughput virtual table implementations (e.g. in-memory key-value stores, LRU caches, ring buffers) require structured row containers that store records with minimal memory bloat, zero fragmentation, and optimal cache locality.

`sqlite3_row.hpp` organizes these responsibilities into **Zero-Allocation Multi-Source Views** and **16-Byte Spans**, both conforming to standard `std::array` interfaces:

```
┌───────────────────────────────────────────────────────────────────────────────────────┐
│                                   SqliteRowView                                       │
│   - Zero dynamic allocations (stack/register resident)                                │
│   - Multi-source tagged union multiplexing:                                           │
│       • Prepared Statements (sqlite3_stmt*)                                           │
│       • UDF / Aggregate Argv (sqlite3_value**)                                        │
│       • Transient View Arrays (const SqliteValueView*)                                │
│       • View Pointer Arrays (const SqliteValueView* const*)                          │
│   - Standard std::array interface: front(), back(), at(), operator[], max_size()     │
│   - Bidirectional Iterators: begin(), end(), rbegin(), rend()                         │
│   [16 Bytes: 8B Tagged Union + 4B Col Count + 1B Source Tag + 3B Pad]                 │
└───────────────────────────────────────────────────────────────────────────────────────┘
                                           ▲
                        .to_vec() / .to_tuple() extraction
                                           │
┌───────────────────────────────────────────────────────────────────────────────────────┐
│                                 SqliteRowOwnedView                                    │
│   - 16-byte universal view over owned memory (2 CPU Registers: rax, rdx)              │
│   - Multiplexes:                                                                      │
│       • Contiguous arrays/spans (const SqliteValueOwned*)                             │
│       • Non-contiguous pointer arrays (const SqliteValueOwned* const*)                │
│   - Standard std::array interface: front(), back(), at(), operator[], max_size()     │
│   - Bidirectional Iterators: begin(), end(), rbegin(), rend()                         │
└───────────────────────────────────────────────────────────────────────────────────────┘
                                           ▲
                        .to_view() / constructor conversion
                                           │
┌───────────────────────────────────────────────────────────────────────────────────────┐
│                        SqliteRowOwnedWrapper (Span)                                   │
│   - 16-byte mutable/const span over contiguous SqliteValueOwned arrays (2 Registers)  │
│   - Standard std::array interface: front(), back(), at(), operator[], max_size()     │
│   - Bidirectional Iterators: begin(), end(), rbegin(), rend()                         │
│   - Wraps: SqliteValueTuple<N>, SqliteValueVec<N>, C-arrays, and single values        │
└───────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Universal 16-Byte Multi-Source Row View (`SqliteRowView`)

`SqliteRowView` multiplexes four backing memory models without virtual method tables (`vtable`), function pointers, or heap allocation:

```
Byte Offset:  0                       8                       12   13  15
              ┌───────────────────────┬───────────────────────┬────┬───┐
              │ m_stmt / m_argv /     │ m_col_count           │src │pad│
              │ m_view_array /        │ [4 Bytes]             │[1B]│[3]│
              │ m_view_ptr_array      │                       │    │   │
              │ [8 Bytes Pointer]     │                       │    │   │
              └───────────────────────┴───────────────────────┴────┴───┘
```

### Source Discriminator Tag (`m_source`)
- `SQLITE_ROW_SOURCE_STMT (0)`: Direct column extraction from active prepared statements.
- `SQLITE_ROW_SOURCE_ARGV (1)`: Direct pointer access into SQLite UDF argument arrays (`sqlite3_value**`).
- `SQLITE_ROW_SOURCE_VIEW_ARRAY (2)`: Contiguous in-memory `SqliteValueView*` array.
- `SQLITE_ROW_SOURCE_VIEW_PTR_ARRAY (3)`: Array of `const SqliteValueView*` pointers. Primarily used for extracting primary keys or non-contiguous column projections from complete tabular rows without allocating intermediate buffers or memory copies.
- `SQLITE_ROW_SOURCE_EMPTY (4)`: Null / empty row view.

---

## 3. Universal 16-Byte Owned Row View (`SqliteRowOwnedView`)

`SqliteRowOwnedView` provides a zero-allocation, 16-byte read-only view over owned values, multiplexing contiguous arrays and non-contiguous pointer arrays:

```
Byte Offset:  0                       8                       12   13  15
              ┌───────────────────────┬───────────────────────┬────┬───┐
              │ m_array /             │ m_col_count           │src │pad│
              │ m_ptr_array           │ [4 Bytes]             │[1B]│[3]│
              │ [8 Bytes Pointer]     │                       │    │   │
              └───────────────────────┴───────────────────────┴────┴───┘
```

### Source Discriminator Tag (`m_source`)
- `SQLITE_ROW_OWNED_SOURCE_ARRAY (0)`: Contiguous in-memory `const SqliteValueOwned*` array (or `SqliteRowOwnedWrapper`, `SqliteValueTuple`, `SqliteValueVec`).
- `SQLITE_ROW_OWNED_SOURCE_PTR_ARRAY (1)`: Array of `const SqliteValueOwned*` pointers. Used for extracting non-contiguous primary key projections from complete owned rows without intermediate buffer allocations.
- `SQLITE_ROW_OWNED_SOURCE_EMPTY (2)`: Null / empty owned row view.

---

## 3. Reverse Iterator & Arrow Proxy Mechanics (`sqlite_reverse_iterator`)

Iterating over standard contiguous arrays produces lvalue references (`SqliteValueOwned&`), whereas iterating over `SqliteRowView` produces transient prvalue views (`SqliteValueView`).

To support `it->method()` seamlessly on both reference types without dangling pointer bugs or temporary copy hazards, `sqlite_reverse_iterator` defines an inner `ArrowProxy`:

```cpp
template <typename Iter>
class sqlite_reverse_iterator {
    Iter m_current;
public:
    class ArrowProxy {
    private:
        value_type m_val;
    public:
        inline explicit ArrowProxy(const value_type& v) noexcept : m_val(v) {}
        inline const_pointer operator->() const noexcept { return &m_val; }
        inline pointer operator->() noexcept { return &m_val; }
    };
    inline ArrowProxy operator->() const noexcept { return ArrowProxy(operator*()); }
};
```

---

## 4. 16-Byte Span: `SqliteRowOwnedWrapper`

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

## 5. Stack-Allocated Scope Dispatcher (`withSqliteRowOwned`)

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
            SqliteValueTuple<> tup(row.size());
            // Populate and dispatch...
            fn(tup.view());
        } break;
    }
}
```
