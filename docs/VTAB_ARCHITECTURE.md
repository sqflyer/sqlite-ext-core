# Virtual Table Architecture (`sqlite3_vtab.hpp`)

The `sqlite3_module` framework in SQLite is incredibly powerful but inherently hostile to type-safe languages like C++.

---

## 1. The Core Architectural Challenge

A SQLite virtual table module is an array of raw function pointers (`xCreate`, `xConnect`, `xBestIndex`, `xOpen`, etc.). Every one of these functions is passed a type-erased `sqlite3_vtab*` or `sqlite3_vtab_cursor*` C struct. 

In pure C, developers subclass these by declaring a struct whose very first element is `sqlite3_vtab`, relying on standard-layout pointer arithmetic:

```c
// The C way
struct MyVTab {
    sqlite3_vtab base;
    int custom_state;
};
```

In C++, doing this with classes containing `virtual` methods breaks standard layout guarantees due to the hidden `vptr` (vtable pointer) injected by the compiler. Casting `sqlite3_vtab*` directly to a polymorphic C++ object yields **Undefined Behavior**.

---

## 2. Standard-Layout Wrapper Solution

`sqlite3_vtab.hpp` circumvents this issue entirely by wrapping the C++ objects in standard-layout C-structs dynamically allocated by the module router via SQLite's allocator (`sqlite_new`):

```cpp
template<typename VTableType, VTabOptions Options>
class SqliteVTabModule {
private:
    struct TableWrapper {
        sqlite3_vtab base;
        VTableType* instance;  // Safe polymorphic C++ pointer!
        void* raw_state;       // Injected connection-level shared state!
    };
    
    struct CursorWrapper {
        sqlite3_vtab_cursor base;
        SqliteVTabCursor* instance; // Safe polymorphic C++ cursor pointer!
    };
};
```

When SQLite invokes `xColumn(sqlite3_vtab_cursor* pCursor, sqlite3_context* ctx, int N)`:
1. The router safely downcasts `pCursor` to `CursorWrapper*` and `pCursor->pVtab` to `TableWrapper*`.
2. It constructs `SqliteContext(ctx, tab->raw_state)` directly on the stack.
3. Invokes `instance->column(sqlite_ctx, N)`.

---

## 3. Direct State Injection Architecture

```
+========================================================================================================+
| 1. REGISTRATION PHASE (sqlite3_create_module_v2)                                                       |
+========================================================================================================+
| SqliteVTab::define_with_state<AppState, MyTable, Options>(db, "my_table")                              |
|   |                                                                                                    |
|   |---> raw_state = SqliteExtState<AppState>::init(db)  (Allocates shared Entry struct)                |
|   |---> sqlite3_create_module_v2(db, "my_table", &module_def, raw_state, destructor)                   |
|                                                                 |                                      |
|                                                     Passed as `pAux`                                   |
+=================================================================|======================================+
                                                                  v
+========================================================================================================+
| 2. CONNECTION PHASE (xConnect / xCreate)                                                               |
+========================================================================================================+
| int xConnect(sqlite3* db, void* pAux, ...)                                                             |
|   |                               ^                                                                    |
|   |                      `pAux` is `raw_state`                                                         |
|   |                                                                                                    |
|   |---> TableWrapper* wrapper = sqlite_new<TableWrapper>();                                            |
|   |---> wrapper->raw_state = pAux;         <--- Injected into TableWrapper!                            |
|   |---> args.state<AppState>()             <--- Accessible in connect() via args.state<T>()!           |
|   |---> *ppVTab = &wrapper->base;                                                                      |
+=================================================================|======================================+
                                                                  v
+========================================================================================================+
| 3. COLUMN EVALUATION (xColumn)                                                                         |
+========================================================================================================+
| int xColumn(sqlite3_vtab_cursor* pCursor, sqlite3_context* raw_ctx, int N)                             |
|   |                                                                                                    |
|   |---> TableWrapper* tab = reinterpret_cast<TableWrapper*>(pCursor->pVtab);                           |
|   |---> SqliteContext ctx(raw_ctx, tab->raw_state);                                                    |
|   |---> wrapper->instance->column(ctx, N);                                                             |
|           |                                                                                            |
|           +---> AppState* state = ctx.state<AppState>();  (O(1) direct single-instruction extraction!) |
+========================================================================================================+
```

---

## 4. Compile-Time On-The-Fly Options Computation

The `sqlite3_module` function pointer table is generated statically at compile time from `VTabOptions`:

```cpp
static constexpr bool is_writable = ((Options & VTabOptions::Writable) != VTabOptions::ReadOnly) || 
                                    ((Options & VTabOptions::Savepoint) != VTabOptions::ReadOnly);
static constexpr bool is_eponymous = (Options & VTabOptions::Eponymous) != VTabOptions::ReadOnly;
static constexpr int IVER = ((Options & VTabOptions::HasShadow) != VTabOptions::ReadOnly) ? 3 : 
                            (((Options & VTabOptions::Savepoint) != VTabOptions::ReadOnly) ? 2 : 1);

static constexpr sqlite3_module module_def = {
    IVER,
    is_eponymous ? nullptr : xCreate,
    xConnect,
    xBestIndex,
    xDisconnect,
    is_eponymous ? nullptr : xDestroy,
    xOpen,
    xClose,
    xFilter,
    xNext,
    xEof,
    xColumn,
    xRowid,
    is_writable ? xUpdate : nullptr,
    is_writable ? xBegin : nullptr,
    is_writable ? xSync : nullptr,
    is_writable ? xCommit : nullptr,
    is_writable ? xRollback : nullptr,
    ((Options & VTabOptions::Findable) != VTabOptions::ReadOnly) ? xFindFunction : nullptr,
    ((Options & VTabOptions::Renameable) != VTabOptions::ReadOnly) ? xRename : nullptr,
    ((Options & VTabOptions::Savepoint) != VTabOptions::ReadOnly) ? xSavepoint : nullptr,
    ((Options & VTabOptions::Savepoint) != VTabOptions::ReadOnly) ? xRelease : nullptr,
    ((Options & VTabOptions::Savepoint) != VTabOptions::ReadOnly) ? xRollbackTo : nullptr,
    ((Options & VTabOptions::HasShadow) != VTabOptions::ReadOnly) ? xShadowName : nullptr,
    nullptr
};
```

---

## 5. Abstraction Mappings

- **`xBestIndex`**: Wrapped by `SqliteIndexInfo` with type-safe `constraint(i)`, `usage(i)`, `set_estimated_cost()`.
- **`xFindFunction`**: Overloaded via `SqliteFunctionDef` without raw pointer allocations.
- **`xUpdate`**: Dispatched with `SqliteUdfArgs` for safe parameter indexing during `INSERT`, `UPDATE`, and `DELETE`.

---

## 6. Zero Overhead Guarantees
- **No Extra Allocations**: Only the two necessary wrapper structs (`TableWrapper` and `CursorWrapper`) are allocated using `sqlite_new` which transparently routes to `sqlite3_malloc64`.
- **Zero VTable Overhead for Invariant Paths**: Option checks (`is_writable`, `is_findable`, etc.) are computed via `constexpr` template logic, allowing the compiler optimizer to completely dead-code strip unused callbacks at compile time.

---

## 7. Virtual Table Argument & Composite Primary Key Parsing (`sqlite3_vtab_arg.hpp`)

When SQLite creates or connects to a virtual table (`CREATE VIRTUAL TABLE tab USING module(...)`), it passes user arguments in `argv[3..argc-1]`.

`sqlite3_vtab_arg.hpp` provides zero-allocation typed parsing for all argument formats:

### Argument Categorization
1. **Engine Parameters (`SqliteVTabParam`)**:
   - `key=value` format (e.g. `capacity=1024`, `ttl=60`, `mode=strict`, `strict=true`).
   - Typed accessors: `as_int()`, `as_long()`, `as_double()`, `as_size()`, `as_bool()`, `as_str()`.
2. **Column Declarations (`SqliteVTabColumn`)**:
   - Column declarations (e.g. `id INTEGER PRIMARY KEY`, `score REAL NOT NULL`, `tag HIDDEN`).
   - Schema properties: `name()`, `definition()`, `affinity()` (Integer, Text, Blob, Real, Numeric), `flags()` (`NotNull`, `PrimaryKey`, `Unique`, `AutoIncr`, `Hidden`).
3. **Table-Level Constraints (`SqliteVTabConstraint`)**:
   - Multi-column / Composite Primary Keys: `PRIMARY KEY (user_id, device_id, version)`
   - Named constraints: `CONSTRAINT pk_custom PRIMARY KEY (tenant_id, org_id)`
   - Table-level unique, check, and foreign key constraints: `UNIQUE(a, b)`, `CHECK(expr)`, `FOREIGN KEY(a) REFERENCES t(b)`.
   - Iteration: `for_each_column_name(fn)` parses comma-separated column names.

### Unified Primary Key & Index Accessors (`SqliteVTabArgs`)
`SqliteVTabArgs` provides zero-allocation index resolution and primary key aggregation:
- `column_count()`: Total number of declared schema columns (excluding parameters & constraints).
- `param_count()`: Total number of `key=value` engine parameters.
- `constraint_count()`: Total number of table-level constraints.
- `column_index(col_name)`: Returns 0-based column index (e.g. `"payload"` $\rightarrow 2$), or `-1` if not found.
- `column_at(idx)`: Returns the $i$-th declared `SqliteVTabColumn`.
- `for_each_column_indexed(fn)`: Iterates columns passing `(col, col_index)` for direct mapping to `xColumn(i)` or `aConstraint[j].iColumn`.
- `for_each_primary_key(fn)`: Iterates all PK column names regardless of whether defined inline (`id INT PRIMARY KEY`) or via table constraint (`PRIMARY KEY (a, b)`).
- `primary_key_count()`: Total number of primary key columns.
- `is_composite_primary_key()`: Returns `true` if $\ge 2$ primary key columns exist.
- `is_primary_key_column(col_name)`: Checks if a given column name is part of the primary key.

### Declarative Schema Binding (`SqliteVTabParamSchema`)
Pre-declares expected parameters with fluent type binding and case-insensitive enum validation:
```cpp
static const char* kModes[] = { "normal", "strict", "fast" };
size_t capacity = 1024;
int mode_idx = 0;

SqliteVTabParamSchema schema;
schema.bind_size("capacity", &capacity)
      .bind_enum("mode", kModes, 3, &mode_idx);
schema.parse(vargs);
```

