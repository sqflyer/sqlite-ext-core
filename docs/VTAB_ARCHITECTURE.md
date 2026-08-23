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
