# C++ Row Types & Tabular Abstractions (`sqlite3_row.hpp`)

High-performance, zero-dependency, freestanding C++ RAII wrappers for SQLite multi-column tabular rows. Engineered specifically for SQLite extension authors to enable **zero-allocation row inspection (`SqliteRowView`)**, **stack-allocated row spans (`SqliteRowOwnedWrapper`)**, **standard `std::array` compliance**, **bidirectional & reverse iteration (`sqlite_reverse_iterator`)**, **transparent single-scalar & multi-column comparisons**, and **seamless statement/UDF argument multiplexing**.

> **Architecture Reference**: For an in-depth systems analysis of the 64-bit alignment models, multi-source tagged union multiplexing, assembly-level execution characteristics, and freestanding memory guarantees, see [`docs/ROW_ARCHITECTURE.md`](ROW_ARCHITECTURE.md).  
> **Value Containers Guide**: For owning multi-column primary key tuples and adaptive SBO vectors, see [`docs/VALUE_CONTAINERS_README.md`](VALUE_CONTAINERS_README.md).

---

## 1. Architectural Philosophy: The Row Abstraction Model

In SQLite extension and virtual table development, tabular row data manifests across distinct execution contexts:
1. **Transient Statement Execution (Views)**: Stepping a prepared statement (`sqlite3_step`) exposes column values via `sqlite3_column_*` APIs. These values are owned by SQLite's VDBE engine and are transient. Reading them into dynamic heap containers causes massive allocation overhead.
2. **UDF Parameter Vectors (Views)**: SQLite user-defined functions receive `(int argc, sqlite3_value** argv)` parameter vectors that must be inspected cleanly with bounds safety, standard `std::array` methods, and zero allocations.
3. **Owned Contiguous Buffers (Spans)**: Multi-column value tuples (`SqliteValueTuple<N>`) and vectors (`SqliteValueVec<N>`) need a uniform, non-owning 16-byte span (`SqliteRowOwnedWrapper`) for standard array operations, range-based iteration, reverse traversal, slicing, and heterogeneous hashing.
4. **Scope-Guarded Stack Dispatching**: `withSqliteRowOwned` instantiates exact compile-time stack tuples for sizes 1..8 with zero heap allocations.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            UNIVERSAL ROW VIEW                               │
│ (Non-owning, Zero-Allocation, Multi-Source Universal Row View)              │
│                                                                             │
│                            SqliteRowView                                    │
│   Multiplexes: sqlite3_stmt* | sqlite3_value** argv | Value Views           │
│   [24 Bytes: 16B Tagged Union + 4B Column Count + 1B Source Tag + 3B Pad]   │
│   Standard std::array interface: front(), back(), at(), []                  │
│   Iterators: begin(), end(), cbegin(), cend(), rbegin(), rend()             │
└─────────────────────────────────────────────────────────────────────────────┘
                                     │
           Convert via .to_vec() OR Extract via direct typed accessors
                                     ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                        OWNED VALUE CONTAINERS & SPANS                       │
│ (RAII-managed Contiguous Buffers & 16-Byte Stack Spans)                     │
│                                                                             │
│  SqliteValueTuple<N> (Exact Stack Array)   SqliteValueVec<N> (Adaptive SBO) │
│  [N × 16 Bytes on Stack, 0 Mallocs]        [N × 16B In-Situ / Heap Spill]   │
│                                                                             │
│                            SqliteRowOwnedWrapper                            │
│   [16-Byte Non-Owning Span: SqliteValueOwned* data + int len (2 Registers)] │
│   Standard std::array interface: front(), back(), at(), [], rbegin(), rend()│
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Feature Matrix

| Feature | `SqliteRowView` (`SqliteUdfArgs`) | `SqliteRowOwnedWrapper` | `SqliteValueTuple<N>` | `SqliteValueVec<N>` |
| :--- | :---: | :---: | :---: | :---: |
| **Size in Memory** | **24 Bytes** | **16 Bytes** (2 Registers) | **$N \times 16$ Bytes** | **$N \times 16$ Bytes** |
| **Standard Alignment** | `std::array` | `std::array` | `std::array` | `std::vector` |
| **Allocation Model** | Zero (Non-owning View) | Zero (Non-owning Span) | Stack ($N \in [1..8]$) / Heap ($N = 0$) | Stack SBO ($N \in [1..8]$) / Heap ($N = 0$) |
| **Backing Sources** | Statement, Argv, View Array | Contiguous `SqliteValueOwned` | In-Situ Stack Array | In-Situ Stack / Heap Buffer |
| **Column Count** | Dynamic ($0 \dots N$) | Dynamic ($0 \dots N$) | Fixed ($N$) | Adaptive ($0 \dots N$) |
| **Element Access** | `front`, `back`, `at`, `[]`, `data` | `front`, `back`, `at`, `[]`, `data` | `front`, `back`, `at`, `[]`, `data` | `front`, `back`, `at`, `[]`, `data` |
| **Direct Typed Access** | `.as_int64()`, `.as_text()`... | `.as_int64()`, `.as_text()`... | `.as_int64()`, `.as_text()`... | `.as_int64()`, `.as_text()`... |
| **Bounds Safety** | Returns fallback `SQLITE_NULL` | Returns fallback `SQLITE_NULL` | Returns fallback `SQLITE_NULL` | Returns fallback `SQLITE_NULL` |
| **Iterators** | Forward + Reverse (`rbegin`/`rend`) | Forward + Reverse (`rbegin`/`rend`) | Forward + Reverse (`rbegin`/`rend`) | Forward + Reverse (`rbegin`/`rend`) |
| **Swiss Table Hashing** | `SqliteRowHash` | `SqliteRowHash` | `SqliteRowHash` | `SqliteRowHash` |
| **Transparent B-Tree** | `SqliteRowLess` | `SqliteRowLess` | `SqliteRowLess` | `SqliteRowLess` |

---

## 3. `SqliteRowView` API Reference

`SqliteRowView` (aliased as `SqliteUdfArgs`) is a lightweight, non-owning 24-byte universal view multiplexing prepared statements, UDF arguments, and in-memory value arrays with zero dynamic allocations and complete `std::array` compliance.

### Multi-Source Creation
```cpp
// 1. Wrap an active prepared statement (at SQLITE_ROW)
SqliteStatement stmt(db, "SELECT id, name, score FROM users;");
if (stmt.step() == SQLITE_ROW) {
    SqliteRowView row = stmt.row(); // Or SqliteRowView(stmt.get());
    assert(row.size() == 3);
    assert(row.front().as_int() == 1);
    assert(row.back().as_double() > 0.0);
}

// 2. Wrap UDF / Virtual Table argument vectors
void my_custom_udf(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    SqliteUdfArgs args(argc, argv);
    sqlite3_int64 id = args.as_int64(0);
    SqliteStringView name = args.as_text(1);
}

// 3. Wrap in-memory contiguous SqliteValueView arrays
SqliteValueView view_arr[2] = { SqliteValueView::from_column(stmt.get(), 0), SqliteValueView::from_column(stmt.get(), 1) };
SqliteRowView row(view_arr, 2);
```

### Forward and Reverse Iteration
```cpp
// 1. Forward range-based for loop
for (SqliteValueView val : row) {
    if (val.type() == SQLITE_TEXT) {
        printf("Text: %s\n", val.as_text().data());
    }
}

// 2. Standard reverse iteration using sqlite_reverse_iterator
for (auto it = row.rbegin(); it != row.rend(); ++it) {
    printf("Reverse col type: %d\n", it->type());
}
```

### Transparent Comparisons
```cpp
// 1-column row transparently compares against primitives and scalars
if (row == 42LL) { /* Single integer column match */ }
if (row == "active") { /* Single string column match */ }
if ("active" == row) { /* Symmetric reverse operator */ }

// Multi-column row comparisons against SqliteRowOwnedWrapper spans
if (row == owned_wrapper) { /* Multi-column equality */ }
if (row < other_row_view) { /* Lexicographical column ordering */ }
```

---

## 4. `SqliteRowOwnedWrapper` API Reference

`SqliteRowOwnedWrapper` is a 16-byte non-owning span (`SqliteValueOwned* m_data` + `int m_len`) fitting in 2 CPU registers (`rax`, `rdx`), supporting complete `std::array` accessors and forward/reverse iterators.

```cpp
SqliteValueOwned arr[3];
arr[0] = SqliteValueOwned(101LL);
arr[1] = SqliteValueOwned("Alice");
arr[2] = SqliteValueOwned(98.5);

SqliteRowOwnedWrapper wrapper(arr, 3);
assert(wrapper.size() == 3);
assert(wrapper.max_size() == 3);
assert(wrapper.front().as_int64() == 101);
assert(wrapper.back().as_double() == 98.5);
assert(wrapper.at(1).as_text() == "Alice");

// Reverse iteration
for (auto it = wrapper.rbegin(); it != wrapper.rend(); ++it) {
    printf("Col: %d\n", it->type());
}
```

---

## 5. Scope-Guarded Stack Dispatcher (`withSqliteRowOwned`)

`withSqliteRowOwned` dynamically dispatches runtime column counts to an exact in-situ `SqliteValueTuple<1..8>` allocated directly on the stack with **zero heap allocations**:

```cpp
SqliteStatement stmt(db, "SELECT id, name, score FROM users;");
if (stmt.step() == SQLITE_ROW) {
    SqliteRowView row = stmt.row();

    // Allocates exact stack tuple matching row.size() and invokes lambda:
    withSqliteRowOwned(row, [](SqliteRowOwnedWrapper owned_span) {
        assert(owned_span.size() == 3);
        // Pure stack memory, zero heap allocations!
    });
}
```
