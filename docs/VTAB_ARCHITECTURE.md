# Virtual Table Architecture (`sqlite3_vtab.hpp` & `sqlite3_vtab_arg.hpp`)

The `sqlite3_module` framework in SQLite is incredibly powerful but inherently hostile to type-safe languages like C++. This document outlines the architectural patterns, standard-layout wrappers, memory layouts, and argument parsing state machines used to build zero-overhead, type-safe SQLite Virtual Tables.

> **Deep Component References**:
> - [Virtual Table Developer Guide (`docs/VTAB_README.md`)](VTAB_README.md)
> - [Virtual Table Argument Parser Architecture (`docs/VTAB_ARG_ARCHITECTURE.md`)](VTAB_ARG_ARCHITECTURE.md)
> - [Virtual Table Argument Parser Developer Guide (`docs/VTAB_ARG_README.md`)](VTAB_ARG_README.md)

---

## 1. The Core Architectural Challenge

A SQLite virtual table module is an array of raw C function pointers (`xCreate`, `xConnect`, `xBestIndex`, `xOpen`, etc.). Every one of these functions is passed a type-erased `sqlite3_vtab*` or `sqlite3_vtab_cursor*` C struct. 

In pure C, developers subclass these structs by embedding `sqlite3_vtab` as the first field, relying on standard-layout pointer arithmetic:

```c
// The C way
struct MyVTab {
    sqlite3_vtab base;
    int custom_state;
};
```

In C++, attempting to inherit or embed `sqlite3_vtab` inside a class with `virtual` methods breaks standard layout guarantees due to the hidden compiler-injected `vptr` (vtable pointer). Casting `sqlite3_vtab*` directly to a polymorphic C++ object yields **Undefined Behavior**.

---

## 2. Standard-Layout Wrapper Solution

`sqlite3_vtab.hpp` circumvents this issue entirely by wrapping C++ class instances inside standard-layout C-structs dynamically allocated by the module router via SQLite's native allocator (`sqlite_new`):

```cpp
template<typename VTableType, VTabOptions Options>
class SqliteVTabModule {
private:
    struct TableWrapper {
        sqlite3_vtab base;
        VTableType*  instance;   // Safe polymorphic C++ pointer!
        void*        raw_state;  // Injected connection-level shared state!
    };
    
    struct CursorWrapper {
        sqlite3_vtab_cursor base;
        SqliteVTabCursor*   instance; // Safe polymorphic C++ cursor pointer!
    };
};
```

When SQLite invokes `xColumn(sqlite3_vtab_cursor* pCursor, sqlite3_context* ctx, int N)`:
1. The router safely downcasts `pCursor` to `CursorWrapper*` and `pCursor->pVtab` to `TableWrapper*`.
2. It constructs a lightweight `SqliteContext(ctx, tab->raw_state)` directly on the stack.
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

## 4. Compile-Time Options Computation (`VTabOptions`)

The `sqlite3_module` function pointer table is generated statically at compile time using `constexpr` evaluation:

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

## 5. Unified Error Propagation Architecture (`zErrMsg` Lifecycle)

SQLite virtual tables report descriptive error messages by allocating a null-terminated string via `sqlite3_mprintf` / `sqlite3_malloc` and assigning it to `pVTab->zErrMsg`.

### Lifecycle & Memory Ownership
1. **Error Trigger**: When any user method (`update`, `filter`, `next`, etc.) returns a non-zero code, the router invokes `set_error_message(pVTab, instance)`.
2. **Querying Error State**: `set_error_message` checks `instance->get_error_message()`. If non-null and `pVTab->zErrMsg` is not yet set, it calls `sqlite3_mprintf("%s", err)`.
3. **SQLite Engine Consumption**: SQLite reads `pVTab->zErrMsg`, incorporates the text into `sqlite3_errmsg(db)`, and frees `zErrMsg` using `sqlite3_free`.
4. **Leak-Free Disconnect**: During `xDisconnect`, any active `zErrMsg` is explicitly freed with `sqlite3_free(wrapper->base.zErrMsg)` to guarantee zero memory leaks under AddressSanitizer.

```cpp
static inline void set_error_message(sqlite3_vtab* pVTab, VTableType* instance) {
    if (pVTab && instance) {
        const char* err = instance->get_error_message();
        if (err && !pVTab->zErrMsg) {
            pVTab->zErrMsg = sqlite3_mprintf("%s", err);
        }
    }
}
```

---

## 6. Virtual Table Argument & Schema Parser (`sqlite3_vtab_arg.hpp`)

When SQLite executes `CREATE VIRTUAL TABLE tab USING module(...)`, it delivers arguments as raw C-strings in `argv[3..argc-1]`. `sqlite3_vtab_arg.hpp` provides zero-allocation parsing across all argument types:

### A. Argument Classification & Tagged Union (`SqliteVTabArg`)
Arguments are classified in a single pass into:
1. **Engine Parameters (`SqliteVTabParam`)**: `key=value` pairs (`capacity=1024`, `mode='strict'`).
2. **Column Declarations (`SqliteVTabColumn`)**: `id INTEGER PRIMARY KEY`, `score REAL NOT NULL`, `tag HIDDEN`.
3. **Table Constraints (`SqliteVTabConstraint`)**: `PRIMARY KEY (tenant_id, device_id)`, `CONSTRAINT pk_name UNIQUE (a, b)`.
4. **Table Options (`Kind::Option`)**: `WITHOUT ROWID`.

### B. SQLite 5 Official Type Affinities
`col.affinity()` computes the official SQLite type affinity:
- **Integer**: Contains `"INT"`
- **Text**: Contains `"CHAR"`, `"CLOB"`, `"TEXT"`
- **Blob**: Contains `"BLOB"` or is untyped
- **Real**: Contains `"REAL"`, `"FLOA"`, `"DOUB"`
- **Numeric**: All other data types

### C. Multi-PK Aggregator & Integer RowID Aliases
- `vargs.for_each_primary_key(fn)`: Merges inline column PKs (`id INT PRIMARY KEY`) with table composite PKs (`PRIMARY KEY (a, b)`).
- `vargs.is_composite_primary_key()`: Returns `true` if $\ge 2$ PK columns exist.
- `vargs.rowid_alias_column_index()`: Returns the 0-based index of the `INTEGER PRIMARY KEY` rowid alias (or `-1` if table is `WITHOUT ROWID` or composite).

### D. Clean DDL Synthesis (`format_declare_vtab_sql`)
`format_declare_vtab_sql()` formats standard SQL DDL for `sqlite3_declare_vtab()`:
- Emits all declared SQL columns (preserving user `HIDDEN` clauses).
- Injects programmatic hidden columns for Table-Valued Functions (`extra_cols`).
- Emits table-level constraints.
- Appends `WITHOUT ROWID` if specified.
- **Strips engine parameters** (`key=value`) so `sqlite3_declare_vtab` never encounters syntax errors.

---

## 7. Zero-Overhead & Freestanding Guarantees

1. **No Runtime Allocations**: All string views (`SqliteStringView`) are non-owning stack slices pointing into SQLite's `argv` memory.
2. **Freestanding `-nostdlib++`**: No dependency on `<string>`, `<vector>`, or `<memory>`.
3. **Compile-Time Feature Dead-Code Elimination**: Feature checks (`is_writable`, `is_findable`, etc.) use `constexpr` logic, allowing the compiler to completely eliminate unused callback entry points.
