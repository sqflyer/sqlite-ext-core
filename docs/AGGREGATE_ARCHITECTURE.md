# C++ Aggregate Function Architecture (`sqlite3_aggregate.hpp`)

This document details the internal design, memory management model, and lifecycle guarantees behind `sqlite3_aggregate.hpp`.

---

## Architectural Objectives

1. **Zero Extra Heap Allocations**: Leverage SQLite's native `sqlite3_aggregate_context` memory region to store the C++ aggregate state without secondary malloc calls.
2. **Deterministic Placement & RAII Cleanup**: Safely invoke constructors (`sqlite_construct_at`) on the first row and destructors (`~T()`) upon query finalization without exceptions or standard library headers.
3. **Flexible SFINAE Dispatching**: Compile-time deduction of user method signatures (`step(args)` vs `step(ctx, args)` and typed `finalize()` vs `finalize(ctx)`).
4. **Strict Multi-Group Isolation**: Guarantee complete independence across `GROUP BY` buckets and concurrent queries.

---

## Memory & State Management (`sqlite3_aggregate_context`)

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
+-------------------------------------------------------------+
|  sqlite3_aggregate_context (Engine-Allocated Memory)        |
+-------------------------------------------------------------+
|  bool initialized (1 byte)                                  |
|  [Alignment Padding]                                        |
|  unsigned char storage[sizeof(T)]  <-- Constructed C++ Type |
+-------------------------------------------------------------+
```

### Lifecycle Guarantees

1. **Initial Allocation & Zeroing**:
   - On the first row of an aggregation group, `sqlite3_aggregate_context(ctx, sizeof(AggregateHolder<T>))` allocates the required memory and zeroes it.
   - Because memory is zeroed, `holder->initialized` begins as `false` (0).
2. **In-Place Construction**:
   - `step_proxy` detects `!holder->initialized`, invokes `sqlite_construct_at(holder->instance())`, and marks `holder->initialized = true`.
3. **Row Stepping**:
   - For every row in the group, `step_proxy` wraps `(argc, argv)` in `SqliteUdfArgs` and calls `agg->step(...)`.
4. **Finalization & Cleanup**:
   - In `final_proxy`, `sqlite3_aggregate_context(ctx, 0)` retrieves the existing pointer.
   - If `holder != nullptr` and `holder->initialized`:
     - Invokes `agg->finalize(...)` and sets the SQL result.
     - Calls explicit destructor `holder->instance()->~T()`.
     - Marks `holder->initialized = false`.
   - SQLite automatically frees the aggregate context memory block after `xFinal` returns.

---

## Empty Set Handling

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
If the struct returns a default value (like `MyCount` returning `0`) or checks for `count == 0` and calls `sqlite3_result_null(ctx)`, it executes cleanly and safely.

---

## Compile-Time SFINAE Return Dispatching

To allow user structs to return raw types or SQLite wrappers directly without boilerplate, `sqlite3_aggregate.hpp` uses lightweight compile-time method disambiguation:

```
                  +--------------------------------+
                  |  T::finalize() Invocation      |
                  +--------------------------------+
                                  |
            +---------------------+---------------------+
            |                                           |
  (Priority 1: int rank)                      (Priority 2: long rank)
  decltype(agg->finalize(ctx))                decltype(agg->finalize())
            |                                           |
            v                                           v
  Direct Context Call                         set_sqlite_result(ctx, ...)
  (Manual Error/Result)                       (Auto-Converted Primitive/Wrapper)
```

Supported automatic conversions:
- `int` $\rightarrow$ `sqlite3_result_int`
- `sqlite3_int64` $\rightarrow$ `sqlite3_result_int64`
- `double` $\rightarrow$ `sqlite3_result_double`
- `bool` $\rightarrow$ `sqlite3_result_int(1/0)`
- `const char*` $\rightarrow$ `sqlite3_result_text(TRANSIENT)`
- `SqliteStringView` / `SqliteStringOwned` $\rightarrow$ `val.result(ctx)`
- `SqliteBlobView` / `SqliteBlobOwned` $\rightarrow$ `val.result(ctx)`
- `SqliteValueView` / `SqliteValueOwned` $\rightarrow$ `val.result(ctx)`

---

---

## Type Safety & Base Class Enforcement (`SqliteAggregateBase<ReturnType>`)

To prevent arbitrary types from being registered as aggregates and produce clear, early compiler diagnostics, `SqliteAggregate<T>` enforces inheritance from `SqliteAggregateBase<ReturnType>` via compile-time verification:

```cpp
struct SqliteAggregateMarker {};

template <typename ReturnType = void>
class SqliteAggregateBase : public SqliteAggregateMarker {
public:
    virtual ~SqliteAggregateBase() {}
    virtual void step(SqliteUdfArgs args) { (void)args; }
    virtual void step(sqlite3_context* ctx, SqliteUdfArgs args) { (void)ctx; step(args); }
    virtual ReturnType finalize() { return ReturnType(); }
};

template <>
class SqliteAggregateBase<void> : public SqliteAggregateMarker {
public:
    virtual ~SqliteAggregateBase() {}
    virtual void step(SqliteUdfArgs args) { (void)args; }
    virtual void step(sqlite3_context* ctx, SqliteUdfArgs args) { (void)ctx; step(args); }
    virtual void finalize(sqlite3_context* ctx) { (void)ctx; }
};
```

### Zero-Dependency `is_base_of` SFINAE Check
In `-nostdlib++` mode where `<type_traits>` is unavailable, `sqlite3_aggregate.hpp` includes a zero-dependency inheritance trait:

```cpp
template <typename Base, typename Derived>
struct is_base_of {
private:
    typedef char yes[1];
    typedef char no[2];

    static yes& test(Base*);
    static no&  test(...);

public:
    static const bool value = sizeof(test(static_cast<Derived*>(nullptr))) == sizeof(yes);
};
```

If a developer attempts to register a struct without inheriting from `SqliteAggregateBase<ReturnType>`, the static assertion immediately fires at compile time:
```cpp
static_assert(SqliteAggregateDetail::is_base_of<SqliteAggregateMarker, T>::value,
              "Custom aggregate struct must publicly inherit from SqliteAggregateBase<ReturnType>!");
```
