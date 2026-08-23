# C++ Aggregate Function Architecture (`sqlite3_aggregate.hpp`)

This document details the internal design, memory management model, 4-tier tag-dispatch SFINAE return hierarchy, and shared state lifecycle guarantees implemented by `sqlite3_aggregate.hpp`.

> **API & Usage Guide**: For tutorials, examples, and the public API reference, see [`docs/AGGREGATE_README.md`](AGGREGATE_README.md).

---

## 1. Architectural Objectives

1. **Zero Extra Heap Allocations**: Leverage SQLite's native `sqlite3_aggregate_context` memory region to store the C++ aggregate state without secondary malloc calls.
2. **Deterministic Placement & RAII Cleanup**: Safely invoke in-place constructors (`sqlite_construct_at`) on the first row and destructors (`~T()`) upon query finalization without exceptions or standard library headers.
3. **4-Tier SFINAE Return Dispatching**: Compile-time deduction of user method signatures (`step(args)` vs `step(ctx, args)` and typed `finalize(ctx)` vs typed `finalize()` vs `void finalize(ctx)`).
4. **Per-Connection Shared State Integration**: Seamlessly share state with Scalar UDFs, TVFs, and other Aggregates using `SqliteExtState<State>` and `define_aggregate_with_state`.
5. **Strict Multi-Group Isolation**: Guarantee complete independence across `GROUP BY` buckets and concurrent queries.

---

## 2. Memory & State Management (`sqlite3_aggregate_context`)

SQLite manages aggregation state memory on behalf of extension functions via `sqlite3_aggregate_context(ctx, nBytes)`.

### Memory Layout
`SqliteAggregate<T>` wraps the user's aggregate struct inside an internal holder:

```cpp
template <typename T>
struct AggregateHolder {
    bool initialized;
    alignas(T) unsigned char storage[sizeof(T)];

    inline T* instance() {
        return reinterpret_cast<T*>(storage);
    }
};
```

```
+=============================================================+
| sqlite3_aggregate_context (Engine-Allocated Memory)         |
+=============================================================+
| bool initialized (1 byte)                                   |
| [Alignment Padding]                                         |
| unsigned char storage[sizeof(T)]  <-- Constructed C++ Type  |
+=============================================================+
```

### Lifecycle Pipeline

```
+-----------------------------------------------------------------------------------------------+
| SQL Query: SELECT my_agg(val) FROM t GROUP BY dept;                                           |
+-----------------------------------------------------------------------------------------------+
       |
       | 1. First Row of Group:
       v
+-------------------------------+
| sqlite3_aggregate_context(ctx)| ---> Allocates & zeroes AggregateHolder<T> block
+---------------+---------------+
       |
       v
+-------------------------------+
| sqlite_construct_at(instance) | ---> In-place constructor: new (instance) T()
| initialized = true            |
+---------------+---------------+
       |
       | 2. Row Iteration:
       v
+-------------------------------+
| agg->step(ctx, args)          | ---> Dispatches to user step() method
+---------------+---------------+
       |
       | 3. Group Finalization (xFinal):
       v
+-------------------------------+
| SqliteAggregateDetail::       | ---> Evaluates typed or void finalize()
| invoke_finalize(agg, ctx)     |
+---------------+---------------+
       |
       v
+-------------------------------+
| agg->~T()                     | ---> Explicit destructor execution
| initialized = false           |
+---------------+---------------+
       |
       v
+-------------------------------+
| SQLite Engine                 | ---> Automatically frees sqlite3_aggregate_context block
+-------------------------------+
```

---

## 3. Empty Set Handling

When a query aggregates over 0 rows (e.g., `SELECT my_avg(x) FROM empty_table`), SQLite never calls `xStep`. Consequently, when `xFinal` is executed, `sqlite3_aggregate_context(ctx, 0)` returns `nullptr`.

To prevent crashes and enable intuitive defaults:
```cpp
if (holder && holder->initialized) {
    SqliteAggregateDetail::invoke_finalize(holder->instance(), ctx);
    holder->instance()->~T();
    holder->initialized = false;
} else {
    // No rows stepped: evaluate finalize on a temporary default instance
    T empty_agg;
    SqliteAggregateDetail::invoke_finalize(&empty_agg, ctx);
}
```

If the struct returns a default value (like `MyCount` returning `0`) or checks for `count == 0` and calls `ctx.result_null()`, it executes cleanly and safely.

---

## 4. 4-Tier Tag-Dispatch SFINAE Hierarchy

To support context-aware typed returns, context-aware void returns, stateless typed returns, and stateless void returns with zero runtime overhead, `sqlite3_aggregate.hpp` uses a compile-time tag dispatch ranking:

```
Rank 0 (Most Specific) ----> Rank 1 ----> Rank 2 ----> Rank 3 (Fallback)
```

```cpp
struct Rank3 {};
struct Rank2 : Rank3 {};
struct Rank1 : Rank2 {};
struct Rank0 : Rank1 {};
```

```
                  +----------------------------------------------+
                  | SqliteAggregateDetail::invoke_finalize(agg)  |
                  +----------------------------------------------+
                                         |
                                         v
                 +------------------------------------------------+
                 | PRIORITY 0: Context-Aware Typed Finalize       |
                 | decltype(set_sqlite_result(ctx,                |
                 |          agg->finalize(SqliteContext(ctx))))   |
                 +-----------------------+------------------------+
                                         | (SFINAE Fallback)
                                         v
                 +------------------------------------------------+
                 | PRIORITY 1: Context-Aware Void Finalize        |
                 | decltype(agg->finalize(SqliteContext(ctx)))    |
                 +-----------------------+------------------------+
                                         | (SFINAE Fallback)
                                         v
                 +------------------------------------------------+
                 | PRIORITY 2: Stateless Typed Finalize           |
                 | decltype(set_sqlite_result(ctx,                |
                 |          agg->finalize()))                     |
                 +-----------------------+------------------------+
                                         | (SFINAE Fallback)
                                         v
                 +------------------------------------------------+
                 | PRIORITY 3: Stateless Void Finalize            |
                 | decltype(agg->finalize())                      |
                 +------------------------------------------------+
```

### Supported Return Conversions
When using typed returns (`Priority 0` or `Priority 2`), `set_sqlite_result` automatically maps:
- `int`, `short`, `char` $\to$ `sqlite3_result_int`
- `sqlite3_int64`, `long long`, `size_t` $\to$ `sqlite3_result_int64`
- `double`, `float` $\to$ `sqlite3_result_double`
- `bool` $\to `sqlite3_result_int(0 or 1)`
- `const char*` $\to `sqlite3_result_text(..., SQLITE_TRANSIENT)`
- `SqliteStringView`, `SqliteStringOwned` $\to `val.result(ctx)`
- `SqliteBlobView`, `SqliteBlobOwned` $\to `val.result(ctx)`
- `SqliteValueView`, `SqliteValueOwned` $\to `val.result(ctx)`

---

## 5. Stateful Aggregates Architecture (`define_with_state`)

When an aggregate is registered via `SqliteAggregate::define_with_state<State, MyAgg>(db, "my_agg")`:

```
+========================================================================================================+
| 1. REGISTRATION PHASE (sqlite3_create_function_v2)                                                     |
+========================================================================================================+
| SqliteAggregate::define_with_state<AppState, MyAgg>(db, "my_agg", 1)                                   |
|   |                                                                                                    |
|   |---> raw_state = SqliteExtState<AppState>::init(db)  (Allocates shared Entry struct)                |
|   |---> sqlite3_create_function_v2(db, "my_agg", 1, SQLITE_UTF8, raw_state,                            |
|   |                                step_proxy, final_proxy, nullptr, nullptr, destructor)              |
|                                         |                                                              |
|                               Passed as `pApp`                                                         |
+=========================================|==============================================================+
                                          v
+========================================================================================================+
| 2. ROW & FINAL EVALUATION (Inside step() / finalize())                                                 |
+========================================================================================================+
| void step(SqliteContext ctx, SqliteUdfArgs args) override {                                            |
|     AppState* state = ctx.state<AppState>();                                                           |
|     //                ^                                                                                |
|     //                +--- Direct O(1) fetch from sqlite3_user_data(ctx.get())                         |
| }                                                                                                      |
+========================================================================================================+
```

### Key Architectural Guarantees:
1. **Zero Lookup Overhead**:
   Unlike Virtual Tables where `user_data` must be injected from the VTab, SQLite aggregates receive `pApp` directly via `sqlite3_user_data(ctx)`. `ctx.state<State>()` executes in **1 single CPU instruction**.
2. **Multi-Group Concurrency**:
   Across different `GROUP BY` groups in a query, each group receives its own independent `AggregateHolder<T>` instance in `sqlite3_aggregate_context`. All groups share the connection-level `SqliteExtState<State>` thread-safely via `ReadGuard` and `WriteGuard`.
3. **Automated Destruction on Database Close**:
   When the SQLite database connection is closed (`sqlite3_close` / `sqlite3_close_v2`), SQLite invokes `SqliteExtState<State>::destructor`, decrementing the reference count and safely freeing the state memory when `ref_count == 0`.
