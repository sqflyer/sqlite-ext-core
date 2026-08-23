# C++ TVF Framework Architecture (`sqlite3_tvf.hpp`)

This document details the internal design, static trampoline generation, `xBestIndex` constraint routing, stateful lifecycle management, and `-nostdlib++` memory safety models implemented by `sqlite3_tvf.hpp`.

> **API & Usage Guide**: For usage tutorials, examples, and the public API reference, see [`docs/TVF_README.md`](TVF_README.md).

---

## 1. Architectural Objectives

1. **Eradicate C-Boilerplate**: SQLite Virtual Tables require coordinating `sqlite3_module`, `sqlite3_vtab`, and `sqlite3_vtab_cursor` C-structs alongside 20+ function callbacks. The goal is to encapsulate this entirely behind a single Object-Oriented `SqliteTvfIterator`.
2. **Automate Query Planner Integration**: The `xBestIndex` method is notoriously difficult to implement correctly. The framework must generically map SQLite index constraints to input arguments without manual user intervention.
3. **Stateful Streaming Integration**: Seamlessly interface with `SqliteExtState<T>` so TVFs can stream internal state or mutate per-connection contexts with thread-safe locking and automated SQLite garbage collection.
4. **`-nostdlib++` Freestanding Portability**: Operate completely without `<memory>`, `<functional>`, RTTI, or exceptions, avoiding runtime dependencies on a global `operator delete`.
5. **Natural Value Type Integration**: Seamlessly interface with `SqliteContext`, `SqliteUdfArgs`, `SqliteStringView`, and `SqliteBlobView`.

---

## 2. Table-Valued Function Execution Lifecycle

When SQLite executes an eponymous Table-Valued Function (e.g. `SELECT * FROM generate_series(1, 10)`), the query engine orchestrates the following callback sequence:

```
+-----------------------------------------------------------------------------------------------+
| SQL Query: SELECT * FROM my_tvf(1, 10)                                                        |
+-----------------------------------------------------------------------------------------------+
       |
       | 1. Query Compilation:
       v
+-------------------------------+
| SqliteTvfModule<T>::xBestIndex| ---> Maps HIDDEN equality constraints to argvIndex (1..N)
+---------------+---------------+ ---> Computes estimatedCost = T::estimated_cost(args)
       |
       | 2. Execution Start:
       v
+-------------------------------+
| SqliteTvfModule<T>::xOpen     | ---> Allocates Cursor & instantiates user iterator: sqlite_new<T>()
+---------------+---------------+
       |
       | 3. Parameter Binding:
       v
+-------------------------------+
| SqliteTvfModule<T>::xFilter   | ---> Wraps (argc, argv) in SqliteUdfArgs & calls iter->init(args)
+---------------+---------------+
       |
       | 4. Row Iteration Loop:
       v
+===============================================================================================+
|                                    ITERATION PIPELINE                                         |
|                                                                                               |
|   +---------------------------+       False        +----------------------------+             |
|   | SqliteTvfModule<T>::xEof  | -----------------> | SqliteTvfModule<T>::xColumn|             |
|   | (Checks iter->eof())      |                    | (Calls iter->column(ctx,i))|             |
|   +-------------+-------------+                    +--------------+-------------+             |
|                 | True                                            |                           |
|                 |                                                 v                           |
|                 |                                  +----------------------------+             |
|                 |                                  | SqliteTvfModule<T>::xNext  |             |
|                 |                                  | (Calls iter->next())       |             |
|                 |                                  +--------------+-------------+             |
|                 |                                                 |                           |
|                 |                                                 v                           |
|                 |                                  (Advances iterator state)                  |
|                 |                                                 |                           |
|                 +<------------------------------------------------+                           |
+===============================================================================================+
       |
       | 5. Stream Completion / Query Finalize:
       v
+-------------------------------+
| SqliteTvfModule<T>::xClose    | ---> Calls iter->~T() and frees cursor via sqlite_delete
+-------------------------------+
```

---

## 3. Template Metaprogramming Bridge (`SqliteTvfModule<T>`)

To avoid inheritance overhead and runtime dynamic dispatch for module registration, the framework uses a templated static bridge:

```cpp
template <typename T>
struct SqliteTvfModule {
    struct VTab { sqlite3_vtab base; };
    struct Cursor { sqlite3_vtab_cursor base; SqliteTvfIterator* iter; };

    // Static C callbacks mapping SQLite pointers back to T*
    static int xConnect(sqlite3* db, void*, int, const char* const*, sqlite3_vtab** ppVtab, char**);
    static int xBestIndex(sqlite3_vtab*, sqlite3_index_info*);
    static int xOpen(sqlite3_vtab*, sqlite3_vtab_cursor**);
    static int xFilter(sqlite3_vtab_cursor*, int, const char*, int, sqlite3_value**);
    static int xNext(sqlite3_vtab_cursor*);
    static int xEof(sqlite3_vtab_cursor*);
    static int xColumn(sqlite3_vtab_cursor*, sqlite3_context*, int);
    static int xRowid(sqlite3_vtab_cursor*, sqlite3_int64*);
    static int xClose(sqlite3_vtab_cursor*);
    // ...
};
```

When `SqliteUdf::define_tvf<MyIterator>(db, "my_tvf")` is called, the compiler generates a dedicated static `sqlite3_module` table populated with zero-cost function pointers.

---

## 4. Stateful TVFs (`define_tvf_with_state`) Architecture

When a TVF is registered via `SqliteUdf::define_tvf_with_state<State, Iterator>(db, name)`:

```
+========================================================================================================+
| 1. REGISTRATION PHASE (sqlite3_create_module_v2)                                                       |
+========================================================================================================+
| SqliteUdf::define_tvf_with_state<AppState, MyTvf>(db, "my_tvf")                                        |
|   |                                                                                                    |
|   |---> raw_state = SqliteExtState<AppState>::init(db)  (Allocates shared Entry struct)                |
|   |---> sqlite3_create_module_v2(db, "my_tvf", &module_def, raw_state, destructor)                     |
|                                                                 |                                      |
|                                                     Passed as `pClientData`                            |
+=================================================================|======================================+
                                                                  v
+========================================================================================================+
| 2. CONNECTION PHASE (SQLite invokes xConnect)                                                          |
+========================================================================================================+
| int xConnect(sqlite3* db, void* pAux, ...)                                                             |
|   |                               ^                                                                    |
|   |                      `pAux` is `raw_state`                                                         |
|   |                                                                                                    |
|   |---> VTab* pTab = sqlite_new<VTab>();                                                               |
|   |---> pTab->raw_state = pAux;            <--- Stored permanently on the Virtual Table instance!      |
|   |---> *ppVtab = &pTab->base;                                                                         |
+=================================================================|======================================+
                                                                  v
+========================================================================================================+
| 3. ROW EVALUATION PHASE (SQLite invokes xColumn)                                                       |
+========================================================================================================+
| int xColumn(sqlite3_vtab_cursor* cur, sqlite3_context* raw_ctx, int col_idx)                           |
|   |                                                                                                    |
|   |---> VTab* pTab = reinterpret_cast<VTab*>(cur->pVtab);   (Access table struct from cursor)          |
|   |---> SqliteContext ctx(raw_ctx, pTab->raw_state);        <--- Injects raw_state into the Context!   |
|   |---> iter->column(ctx, col_idx);                                                                   |
+=================================================================|======================================+
                                                                  v
+========================================================================================================+
| 4. USER ACCESS PHASE (Inside your TVF Iterator class)                                                  |
+========================================================================================================+
| void column(SqliteContext ctx, int col_idx) override {                                                 |
|     AppState* state = ctx.state<AppState>();                                                           |
|     //                ^                                                                                |
|     //                +--- Calls: ctx.user_data() -> returns injected `m_user_data`                   |
|     //                +--- Downcasts `Entry*` -> returns `&entry->state` in 1 CPU instruction!         |
| }                                                                                                      |
+========================================================================================================+
```

### Key Architectural Advantages:
1. **Automated Lifecycle on Database Close**:
   When the SQLite database connection is closed (`sqlite3_close` / `sqlite3_close_v2`), SQLite invokes `SqliteExtState<State>::destructor`, decrementing the reference count and safely freeing the state memory when `ref_count == 0`.
2. **Direct Context Injection (Zero-Lookup $O(1)$ State Retrieval)**:
   SQLite's C engine passes `NULL` for `sqlite3_user_data(ctx)` in virtual table `xColumn` callbacks. The TVF framework circumvents this limitation by capturing `pAux` in `xConnect` on `VTab::raw_state`, and injecting it into `SqliteContext(ctx, pTab->raw_state)` during `xColumn`. Calling `ctx.state<State>()` accesses the injected pointer directly in **1 single CPU instruction ($O(1)$)** with zero hash lookups and zero database handle searches.
3. **Cross-Subsystem State Sharing**:
   Scalar UDFs, Aggregates, Table-Valued Functions (TVFs), and Virtual Tables on the same connection all read and write to the exact same thread-safe `SqliteExtState<T>` struct instance.

### 4.1 Multi-TVF & Multi-VTab Stack Isolation Model

When multiple stateful TVFs or Virtual Tables are evaluated within the same query (e.g. `SELECT * FROM tvf_a() JOIN tvf_b()` or correlated subqueries), `SqliteContext` guarantees **100% ephemeral stack isolation**:

```
           +===========================================================+
           | QUERY: SELECT a.val, b.val FROM tvf_a() a JOIN tvf_b() b |
           +===========================================================+
                                       |
                 +---------------------+---------------------+
                 |                                           |
                 v (Row from TVF A)                          v (Row from TVF B)
+------------------------------------+      +------------------------------------+
| STACK FRAME 1 (Invocation Call)    |      | STACK FRAME 2 (Invocation Call)    |
| TVF_A::xColumn(curA, raw_ctx1, 0)  |      | TVF_B::xColumn(curB, raw_ctx2, 0)  |
|                                    |      |                                    |
| pTabA = (VTab*)curA->pVtab;        |      | pTabB = (VTab*)curB->pVtab;        |
|                                    |      |                                    |
| SqliteContext ctxA(raw_ctx1,       |      | SqliteContext ctxB(raw_ctx2,       |
|                    pTabA->raw_state|      |                    pTabB->raw_state|
| );                                 |      | );                                 |
|                                    |      |                                    |
| -> ctxA.state<StateA>()            |      | -> ctxB.state<StateB>()            |
+------------------------------------+      +------------------------------------+
```

#### Key Isolation Guarantees:
1. **Zero Cross-Talk via Local Stack Frames**:
   `SqliteContext` is a 16-byte value type (`sqlite3_context*` + `void* user_data`). It is allocated on the local C++ stack frame of `xColumn` and destroyed the instant `xColumn` returns. There are no shared global context registers or mutable static state.
2. **Same-State Execution (`StateA` == `StateB`)**:
   If TVF A and TVF B share the same state type, both `pTabA->raw_state` and `pTabB->raw_state` hold the exact same physical `Entry*` address. Concurrency is synchronized safely via `ReadGuard` (shared concurrent reads) and `WriteGuard` (exclusive mutations).
3. **Heterogeneous Multi-State Execution (`StateA` != `StateB`)**:
   If TVF A is bound to `StateA` and TVF B is bound to `StateB`, each `VTab` captures its own distinct `raw_state` in `xConnect`. `ctxA.state<StateA>()` and `ctxB.state<StateB>()` resolve independently without risk of cross-type contamination.
4. **Reference-Counted Multi-Module Cleanup**:
   Each module registered with `define_tvf_with_state` increments the connection's atomic reference count. When the database closes, SQLite invokes `xDestroy` on each module; the state remains alive until the last dependent module or UDF drops `ref_count` to zero.

---

## 5. `-nostdlib++` Memory Management: Avoiding `operator delete`

In standard C++, polymorphic base classes require a virtual destructor (`virtual ~SqliteTvfIterator()`). However, compiling with `-nostdlib++` (no standard C++ runtime) creates a fatal problem: deleting an object via a virtual destructor emits a reference to the global runtime `operator delete(void*)`, resulting in linker errors.

`SqliteTvfIterator` solves this through **Static Downcasting RAII**:

```cpp
class SqliteTvfIterator {
protected:
    // Protected non-virtual destructor prevents base pointer deletion
    ~SqliteTvfIterator() = default;
};
```

When SQLite invokes `xClose`, `SqliteTvfModule<T>` explicitly casts the iterator back to the exact derived class `T*` before destroying it:

```cpp
static int xClose(sqlite3_vtab_cursor* cur) {
    Cursor* pCur = reinterpret_cast<Cursor*>(cur);
    
    // 1. Statically downcast and call derived destructor:
    sqlite_delete(static_cast<T*>(pCur->iter));
    
    // 2. Free cursor holder:
    sqlite_delete(pCur);
    return SQLITE_OK;
}
```

This guarantees 100% memory safety and destructor execution without requiring a vtable or standard library dependencies.

---

## 6. Generic `xBestIndex` Auto-Tuning & Cost Optimization

SQLite TVFs receive parameters by declaring them as `HIDDEN` columns in the table schema. When a SQL query invokes `SELECT * FROM my_tvf(10, 20)`, SQLite's query parser translates this to:
```sql
SELECT * FROM my_tvf WHERE arg1 = 10 AND arg2 = 20;
```

`SqliteTvfModule<T>::xBestIndex` automatically maps these constraints:
1. Iterates over all `aConstraint` entries provided by SQLite.
2. Checks equality constraints on hidden columns (`iColumn > 0`).
3. If an input argument constraint is unusable (e.g. referencing an unjoined table), it returns `SQLITE_CONSTRAINT` to gracefully guide the planner.
4. Maps usable constraints to `argvIndex` (1-based index) and sets `omit = 1` to inform SQLite the TVF handled the filter internally.
5. Injects the estimated cost:
   ```cpp
   pIdxInfo->estimatedCost = T::estimated_cost(usable_constraints);
   ```

By default, `SqliteTvfIterator::estimated_cost` returns $\frac{100000.0}{\text{usable\_constraints} + 1}$, ensuring the query optimizer always favors execution plans that provide the maximum number of input arguments.
