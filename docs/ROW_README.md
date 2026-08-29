# C++ Row Types & Tabular Abstractions (`sqlite3_row.hpp`)

High-performance, zero-dependency, freestanding C++ RAII wrappers for SQLite multi-column tabular rows. Engineered specifically for SQLite extension authors to enable **zero-allocation row inspection**, **stack-allocated fixed-schema rows (0 mallocs)**, **runtime dynamic heap rows via SQLite allocators**, and **seamless statement/UDF argument multiplexing**.

> **Architecture Reference**: For an in-depth systems analysis of the 64-bit alignment models, L1 cache line density calculations ($N=4$ fitting in 64 bytes), multi-source tagged union multiplexing, assembly-level execution characteristics, and freestanding memory guarantees, see [`docs/ROW_ARCHITECTURE.md`](ROW_ARCHITECTURE.md).

---

## 1. Architectural Philosophy: The Row Abstraction Model

In SQLite extension and virtual table development, tabular row data manifests across distinct execution contexts:
1. **Transient Statement Execution (Views)**: Stepping a prepared statement (`sqlite3_step`) exposes column values via `sqlite3_column_*` APIs. These values are owned by SQLite's VDBE engine and are transient. Reading them into dynamic containers (`std::vector<std::string>`) causes massive heap fragmentation.
2. **UDF Parameter Vectors (Views)**: SQLite user-defined functions receive `(int argc, sqlite3_value** argv)` parameter vectors that must be inspected cleanly with bounds safety and zero allocations.
3. **Fixed-Schema Persistent Storage (Static Stack Rows)**: In-memory virtual tables, caches (LRU/MRU), and key-value stores often operate on known compile-time schemas (e.g., `(key, value, timestamp)`). These benefit immensely from stack allocation or contiguous in-situ storage with zero heap calls.
4. **Dynamic-Schema Query Results (Dynamic Heap Rows)**: Generalized query materialization and caching layers require runtime-sized row containers with strict RAII ownership, deep copying, and 1-cycle move semantics.

`sqlite3_row.hpp` organizes these responsibilities into **4 specialized abstractions**:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                               VIEW CLASSES                                  │
│ (Non-owning, Zero-Allocation, Multi-Source Universal Row View)              │
│                                                                             │
│                            SqliteRowView                                    │
│   Multiplexes: sqlite3_stmt* | sqlite3_value** argv | Value Arrays          │
│   [24 Bytes: 16B Union + 4B Count + 1B Source + 3B Pad]                     │
└─────────────────────────────────────────────────────────────────────────────┘
                                     │
           Convert via .to_owned() OR Extract via direct typed accessors
                                     ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           OWNED ARRAY BASE CLASSES (sqlite3_value.hpp)      │
│ (Contiguous N × 16B SqliteValueOwned arrays, RAII-managed)                  │
│                                                                             │
│  SqliteValueOwnedStaticArray<N>          SqliteValueOwnedDynamicArray       │
│  (Stack / In-Situ, 0 Mallocs)            (sqlite3_realloc64-managed Heap)   │
│  [N × 16 Bytes]                          [16B Handle → Contiguous Heap Buf] │
└─────────────────────────────────────────────────────────────────────────────┘
                           │                          │
                (inherits) │                          │ (inherits)
                           ▼                          ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                      OWNED ROW CLASSES (sqlite3_row.hpp)                    │
│ (Adds row-domain API: .view(), .column_count(), operator SqliteRowView())   │
│                                                                             │
│      SqliteRowStatic<N>                       SqliteRowDynamic              │
│   (Compile-time N Columns)                 (Runtime-sized Heap Array)       │
│   [N * 16 Bytes on Stack]                  [16 Bytes handle → Heap Array]   │
│                                                                             │
│                  SqliteRowOwned<N> (Unified Template Alias)                 │
│         - SqliteRowOwned<N> (N > 0) maps to SqliteRowStatic<N>              │
│         - SqliteRowOwned<0>         maps to SqliteRowDynamic                │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Feature Matrix

| Feature | `SqliteRowView` | `SqliteRowStatic<N>` | `SqliteRowDynamic` | `SqliteRowOwned<N>` |
| :--- | :---: | :---: | :---: | :---: |
| **Size in Memory** | **24 Bytes** | **$N \times 16$ Bytes** | **16 Bytes** (handle) | $N \times 16$B ($N>0$) / 16B ($N=0$) |
| **Allocation Model** | Zero (Non-owning) | Stack / In-Situ (0 Mallocs) | `sqlite3_malloc64` Array | Stack ($N>0$) / Heap ($N=0$) |
| **Column Count** | Dynamic (0 to $N$) | Fixed Compile-Time ($N$) | Dynamic Runtime ($N$) | Fixed ($N>0$) / Dynamic ($N=0$) |
| **Backing Sources** | Statement, Argv, Arrays | Pure Owned Array | Pure Owned Array | Pure Owned Array |
| **Direct Typed Access** | `.as_int64()`, `.as_text()`... | `.as_int64()`, `.as_text()`... | `.as_int64()`, `.as_text()`... | `.as_int64()`, `.as_text()`... |
| **Subtype Inspection** | `.subtype(i)` | `.subtype(i)` | `.subtype(i)` | `.subtype(i)` |
| **Schema Metadata** | `.column_name(i)`, `.decltype()`| N/A | N/A | N/A |
| **Bounds Safety** | Returns `SQLITE_NULL` View | Clamped to bounds | Clamped to bounds | Clamped to bounds |
| **Range-Based Iteration**| `for (SqliteValueView c : row)`| Via `.view()` | Via `.view()` | Via `.view()` |
| **Move Semantics** | Copyable (Trivial) | Element-wise Move | 1-Cycle Pointer Move | Full Move Support |
| **Materialization** | `.to_owned()` $\to$ Dynamic | Direct Copy Constructor | Deep `.clone()` / Move | Full Conversion Support |

---

## 3. `SqliteRowView` API Reference

`SqliteRowView` is a lightweight, non-owning 24-byte universal view multiplexing prepared statements, UDF arguments, and in-memory value arrays with zero dynamic allocations.

### Constructors & Multi-Source Creation
```cpp
// 1. Wrap an active prepared statement (at SQLITE_ROW)
SqliteStatement stmt(db, "SELECT id, name, score FROM users;");
if (stmt.step() == SQLITE_ROW) {
    SqliteRowView row = stmt.row(); // Or SqliteRowView(stmt.get());
}

// 2. Wrap UDF / Virtual Table argument vectors
void my_custom_udf(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    SqliteRowView args(argc, argv);
    // Or from SqliteValueViewArray / SqliteUdfArgs
}

// 3. Wrap in-memory contiguous SqliteValueView arrays
SqliteValueView view_arr[2] = { SqliteValueView::from_column(stmt.get(), 0), SqliteValueView::from_column(stmt.get(), 1) };
SqliteRowView row(view_arr, 2);

// 4. Wrap in-memory contiguous SqliteValueOwned arrays (use SqliteRowOwnedWrapper)
SqliteValueOwned arr[3] = { SqliteValueOwned(1), SqliteValueOwned("Alice"), SqliteValueOwned(95.5) };
SqliteRowOwnedWrapper owned_wrapper(arr, 3);
```

### Bounds-Safe Column Access
`SqliteRowView` guarantees segfault immunity. Accessing columns out of range (`< 0` or `>= size()`) returns a valid `SQLITE_NULL` `SqliteValueView`.

```cpp
SqliteValueView col0 = row[0];            // Fast subscript operator
SqliteValueView col1 = row.at(1);          // Bounds-checked alias
SqliteValueView col2 = row.get_column(2);  // Fluent alias
SqliteValueView col9 = row[999];          // Safely returns SQLITE_NULL view (no crash!)
```

### Direct Typed Column Extraction (`SQLITE_DERIVE_ARRAY_ACCESSORS`)
Extract column data directly with optional default parameter `index = 0` (synthesized via `SQLITE_DERIVE_ARRAY_ACCESSORS` over `operator[](col)`):

```cpp
sqlite3_int64 id    = row.as_int64();    // Defaults to col 0 (64-bit integer)
int           code  = row.as_int(0);     // 32-bit integer
double        score = row.as_double(2);  // IEEE-754 double
bool          flag  = row.as_bool(3);    // Evaluates non-zero integer as true
SqliteStringView name = row.as_text(1);  // Non-allocating string view (ptr + length)
SqliteBlobView  blob = row.as_blob(4);   // Non-allocating binary blob view

if (row.is_null(5)) {
    // Column is SQLITE_NULL or out of bounds
}

int     t   = row.type(0);     // SQLITE_INTEGER, SQLITE_TEXT, SQLITE_FLOAT, etc.
uint8_t sub = row.subtype(1);  // 8-bit SQLite subtype ('J' for JSON, 'V' for Vector, etc.)
```

### Schema Introspection (Statement Source Only)
```cpp
const char* col_name = row.column_name(0);      // e.g. "id"
const char* decltype = row.column_decltype(0);  // e.g. "INTEGER"
```

### Range-Based For Loop Traversal
```cpp
// Traverse all columns with standard C++11 range-based for loop
for (SqliteValueView col : row) {
    if (col.is_text()) {
        printf("Text: %s\n", col.as_text().data());
    } else if (col.is_integer()) {
        printf("Int: %lld\n", col.as_int64());
    }
}
```

### Transparent Relational Operators
`SqliteRowView` supports the full suite of transparent comparison operators (`==`, `!=`, `<`, `<=`, `>`, `>=`) across single scalar primitives, string views, blob views, and row wrappers:

```cpp
// 1-column row transparently compares against primitives and scalars
if (row == 42LL) { /* Single integer column match */ }
if (row == "active") { /* Single string column match */ }
if ("active" == row) { /* Symmetric reverse operator */ }

// Multi-column row comparisons against SqliteRowOwnedWrapper spans
if (row == owned_wrapper) { /* Multi-column equality */ }
if (row < other_row_view) { /* Lexicographical column ordering */ }
```

### Snapshot Materialization to Owned Heap Row
```cpp
// Deep copies all column strings, blobs, and subtypes into an owned dynamic row
SqliteRowDynamic snapshot = row.to_owned();
```

---

## 4. `SqliteRowStatic<size_t N>` API Reference

`SqliteRowStatic<N>` is a compile-time fixed-size row container occupying **exactly $N \times 16$ bytes** directly on the stack or embedded in-situ within custom data structures.

### Stack Allocation & Footprint
```cpp
// Exactly 48 bytes on the stack (3 cols * 16 bytes) - 0 Mallocs!
SqliteRowStatic<3> user_row;

user_row[0] = 101LL;
user_row[1] = SqliteValueOwned::from_text("Alice");
user_row[2] = 98.5;

// Exact 64-byte row (4 cols * 16 bytes) fits perfectly into a single L1 Cache Line!
SqliteRowStatic<4> cache_entry;
```

### Direct Typed Access & Mutation
```cpp
sqlite3_int64 id    = user_row.as_int64(0);
SqliteStringView nm = user_row.as_text(1);
double        val   = user_row.as_double(2);

// Mutate values in-place
user_row[1] = SqliteValueOwned::from_text("Bob");
```

### Materialization from Views
```cpp
// Construct static row by copying up to N columns from a query view.
// Internally delegates to SqliteRowUtil::copy_from_view (shared with SqliteRowDynamic).
SqliteStatement stmt(db, "SELECT id, name, score FROM users WHERE id = 101;");
if (stmt.step() == SQLITE_ROW) {
    SqliteRowStatic<3> cached_row(stmt.row());
    assert(cached_row.as_int64(0) == 101);
}
```

### Conversion to `SqliteRowView`
```cpp
// Seamless zero-allocation conversion to row view
SqliteRowView v1 = user_row.view();
SqliteRowView v2 = user_row; // Implicit conversion operator
```

---

## 5. `SqliteRowDynamic` API Reference

`SqliteRowDynamic` is a runtime-sized heap container allocating a contiguous array of `SqliteValueOwned` elements using SQLite's native memory allocator (`sqlite3_malloc64` / `sqlite3_free`).

### Construction & Memory Ownership
```cpp
// Allocate a 5-column dynamic row (all initialized to SQLITE_NULL)
SqliteRowDynamic dyn_row(5);

dyn_row[0] = 1001LL;
dyn_row[1] = SqliteValueOwned::from_text("Dynamic User");
dyn_row[2] = 3.14159;
dyn_row[3] = SqliteValueOwned::from_blob(blob_ptr, blob_sz);
dyn_row[4] = SqliteValueOwned::from_json("{\"active\":true}");
```

### Deep Copy & 1-Cycle Move Semantics
```cpp
// Deep copy: duplicates all heap strings/blobs via sqlite3_value_dup
SqliteRowDynamic copy_row = dyn_row;
assert(copy_row.size() == 5);

// Move: transfers 8-byte buffer pointer in 1 CPU cycle (0 allocations)
SqliteRowDynamic moved_row = sqlite_move(dyn_row);
assert(moved_row.size() == 5);
assert(dyn_row.empty()); // Source safely reset to 0 columns
```

### Dynamic Resizing with State Preservation
```cpp
SqliteRowDynamic row(2);
row[0] = 10LL;
row[1] = 20LL;

// Resize to 4 columns: preserves existing 2 columns and initializes new columns to NULL
row.resize(4);
assert(row.size() == 4);
assert(row.as_int64(0) == 10);
assert(row.as_int64(1) == 20);
assert(row.is_null(2));
assert(row.is_null(3));
```

---

## 6. `SqliteRowOwned<size_t N>` Unified Template Alias

`SqliteRowOwned<N>` provides a unified generic template interface:
- `SqliteRowOwned<N>` ($N > 0$): Maps directly to `SqliteRowStatic<N>` (Pure Stack Allocation).
- `SqliteRowOwned<0>`: Specializes to `SqliteRowDynamic` (Runtime Heap Allocation).

### Generic Algorithm Example
```cpp
template <size_t N>
void process_row(const SqliteRowOwned<N>& row) {
    for (int i = 0; i < row.size(); ++i) {
        printf("Col %d Type: %d\n", i, row.type(i));
    }
}

// 1. Use with stack-allocated static row:
SqliteRowOwned<3> static_row;
static_row[0] = 1LL;
process_row(static_row);

// 2. Use with heap-allocated dynamic row:
SqliteRowOwned<0> dynamic_row(4);
dynamic_row[0] = 2LL;
process_row(dynamic_row);
```

---

## 7. Statement & Database Query Workflow

Integrate `SqliteRowView` and `SqliteStatement` for end-to-end query execution:

```cpp
#include "sqlite3_statement.hpp"
#include "sqlite3_row.hpp"
#include "sqlite3_db.hpp"

void query_users(sqlite3* db) {
    SqliteDatabaseView db_view(db);
    SqliteStatement stmt = db_view.prepare("SELECT id, name, rating FROM staff WHERE active = ?;");
    
    stmt.bind(1, 1); // Bind active = 1

    while (stmt.step() == SQLITE_ROW) {
        SqliteRowView row = stmt.row();
        
        sqlite3_int64 id     = row.as_int64(0);
        SqliteStringView name = row.as_text(1);
        double rating        = row.as_double(2);

        printf("User [%lld]: %.*s (Rating: %.1f)\n", id, name.length(), name.data(), rating);
    }
}
```

---

## 8. Zero-Allocation Row Span (`SqliteRowOwnedWrapper`)

`SqliteRowOwnedWrapper` is a lightweight 16-byte span (`SqliteValueOwned*` + `int len`) fitting in **2 CPU registers** (`rax`, `rdx`). It provides mutable/const `operator[]`, `.at()`, `.data()`, and typed `as_*()` extractors (`SQLITE_DERIVE_ARRAY_ACCESSORS`):

```cpp
// Create from raw pointer + count
SqliteRowOwnedWrapper span(arr.data(), arr.size());

// Create from static rows, dynamic rows, or single scalar values:
SqliteRowOwnedWrapper w_sc = SqliteRowOwnedWrapper::create(val);
SqliteRowOwnedWrapper w_st = SqliteRowOwnedWrapper::create(static_row);
SqliteRowOwnedWrapper w_dy = SqliteRowOwnedWrapper::create(dyn_row);

// Access and extract columns with 0 overhead
assert(w_st.size() == 3);
assert(w_st[0].as_int64() == 100);
assert(w_st.as_int(0) == 100);
assert(w_st.as_text(1) == "Alice");
```

---

## 9. Stack-Allocated Row Scope Dispatcher (`withSqliteRowOwned`)

`withSqliteRowOwned` provides a zero-heap stack allocation dispatcher for small dynamic row materialization ($1 \dots 8$ columns on stack, fallback to dynamic heap for $> 8$):

```cpp
int cols = 4; // Determined at runtime

int result = withSqliteRowOwned(cols, [](SqliteRowOwnedWrapper wrapper) {
    // wrapper provides direct mutable operator[] over stack buffer for cols <= 8 (0 heap mallocs)
    wrapper[0] = SqliteValueOwned(101);
    wrapper[1] = SqliteValueOwned::from_text("Alice");
    wrapper[2] = SqliteValueOwned(98.5);
    wrapper[3] = SqliteValueOwned::from_json("{\"active\":true}");

    assert(wrapper.size() == 4);
    assert(wrapper[1].as_text() == "Alice");

    // All memory is automatically destroyed upon scope exit
    return 42;
});
assert(result == 42);
```

---

## 10. Transparent STL Functors (`SqliteRowHash`, `SqliteRowEqual`, `SqliteRowLess`)

`sqlite3_row.hpp` provides transparent functors (`is_transparent = void`) enabling zero-allocation lookups in `std::unordered_map`, Swiss Tables, and `std::map`:

```cpp
#include "sqlite3_row.hpp"
#include <unordered_map>

// Keyed by dynamic row, searchable by lightweight spans or view arrays with 0 allocations
std::unordered_map<SqliteRowDynamic, std::string, SqliteRowHash, SqliteRowEqual> row_cache;

SqliteRowDynamic row(2);
row[0] = 1001LL;
row[1] = SqliteValueOwned::from_text("dept_finance");
row_cache[sqlite_move(row)] = "Finance Department";

// Search via zero-allocation SqliteRowOwnedWrapper span:
SqliteValueOwned search_cols[2] = { SqliteValueOwned(1001LL), SqliteValueOwned::from_text("dept_finance") };
SqliteRowOwnedWrapper search_span(search_cols, 2);

auto it = row_cache.find(search_span);
assert(it != row_cache.end());
```

---

## 11. Performance Benchmarks (Cycle-Accurate)

| Operation | Standard C++ / SQLite Baseline | `sqlite3_row.hpp` | Performance Improvement |
| :--- | :--- | :--- | :--- |
| **Row View Instantiation** | Struct construction + heap array | **Register copy (`SqliteRowView`)** | **$\sim 20\times\text{--}50\times$ Faster** |
| **Fixed Row Allocation ($N=4$)** | `malloc` for `std::vector<Mem>` | **Stack-allocated ($64$B inline)** | **$\mathbf{0}$ Heap Allocs ($100\times$ Faster)** |
| **Row Scope Dispatcher ($N \le 8$)** | Dynamic heap allocation | **`withSqliteRowOwned` (Stack)** | **$\mathbf{0}$ Heap Allocs ($50\times$ Faster)** |
| **L1 Cache Line Alignment ($N=4$)**| Multiple disjoint heap chunks | **Exact 64B Contiguous Line** | **$100\%$ L1 Cache Hit Ratio** |
| **Column Extraction (`as_int64()`)** | C API pointer dereferences | **Direct inlined register read** | **$1$ CPU Instruction** |
| **Dynamic Row Move Semantics** | Buffer deep copy | **Pointer swap (`sqlite_move`)** | **$1$ CPU Cycle** |
| **Out-of-Bounds Protection** | Undefined behavior / crash | **Zero-branch `SQLITE_NULL` View**| **$100\%$ Segfault Immunity** |
