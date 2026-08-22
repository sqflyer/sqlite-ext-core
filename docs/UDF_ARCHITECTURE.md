# C++ UDF Builder Architecture (`sqlite3_udf.hpp`)

This document details the internal design and architectural decisions behind `sqlite3_udf.hpp`.

---

## Architectural Objectives

1. **Zero Dynamic Allocation**: Parameter wrapping and function invocation must not trigger any `malloc`, `new`, or standard library heap traffic.
2. **Bounds-Checked Parameter Access**: Protect against segfaults and buffer overruns when extensions access `argv`.
3. **Strict Freestanding Portability**: Operate completely without `<functional>`, `<tuple>`, or RTTI/exceptions (`-nostdlib++ -fno-exceptions -fno-rtti`).
4. **Natural Value Key Integration**: Seamlessly interoperate with `SqliteValueView`, `SqliteStringOwned`, and `SqliteBlobOwned`.

---

## The Trampoline Proxy Pattern

In standard SQLite C extensions, registering a user-defined function requires invoking `sqlite3_create_function_v2` with a raw C callback of signature:
```c
void (*xFunc)(sqlite3_context*, int, sqlite3_value**);
```

Passing C++ member functions or higher-level signatures directly is prevented by C linkage rules. Furthermore, storing function pointers in global variables causes concurrency bottlenecks and violates multi-database isolation.

`SqliteUdf` solves this by implementing a static **Trampoline Proxy**:

```
+------------------+         +-------------------------------+         +-----------------------+
|  SQLite Core     | ------> |  SqliteUdf::scalar_proxy()    | ------> |  User C++ Function    |
|  (sqlite3_step)  |         |  (C Callback Trampoline)      |         |  (or stateless lambda)|
+------------------+         +-------------------------------+         +-----------------------+
                                           |
                              - Reads `sqlite3_user_data(ctx)`
                              - Constructs `SqliteUdfArgs(argc, argv)`
                              - Dispatches to target C++ function
```

### Registration Lifecycle
1. When `SqliteUdf::define(db, name, num_args, func)` is called, the C++ function pointer `func` is cast to `void*` and passed directly as the `pApp` (user data) parameter in `sqlite3_create_function_v2`.
2. The SQLite engine associates this pointer with the function registration in its internal VDBE function table.
3. When SQL queries invoke the function, SQLite passes this pointer back to `scalar_proxy` via `sqlite3_user_data(ctx)`.
4. `scalar_proxy` casts `user_data` back to `ScalarFunc` and executes it.

This eliminates all global state and ensures that every registered UDF operates independently across multiple threads and database connections.

---

## Zero-Allocation Parameter Proxy (`SqliteUdfArgs`)

The `SqliteUdfArgs` class is a lightweight 16-byte stack structure on 64-bit platforms:
```cpp
class SqliteUdfArgs {
    int m_argc;
    sqlite3_value** m_argv;
};
```

### Memory and Runtime Characteristics:
- **Zero Heap Overhead**: Instantiated directly on the execution stack.
- **Copy Cost**: Passed by value or reference with trivial register/stack copies.
- **Index Safety**: `operator[](int index)` checks bounds:
  ```cpp
  inline SqliteValueView operator[](int index) const {
      if (index < 0 || index >= m_argc) {
          return SqliteValueView(nullptr);
      }
      return SqliteValueView(m_argv[index]);
  }
  ```
  If an out-of-bounds index is queried, it returns `SqliteValueView(nullptr)` whose `type()` evaluates to `SQLITE_NULL`, preventing invalid memory dereferencing.

---

## Synergy with Value Keys and Small Buffer Optimization

`SqliteUdfArgs::operator[]` returns a `SqliteValueView`, which directly enables:
- In-place numeric casting via `as_int64()` and `as_double()`.
- Direct heterogeneous comparisons (`args[0] == 42`, `args[0] < 3.14`, `args[0] == SqliteStringView("test")`).
- Safe handoff to `SqliteValueOwned` if long-term storage or map insertion is needed.

---

## Deterministic Execution Optimization

`SqliteUdf::define` defaults `deterministic = true`, automatically attaching the `SQLITE_DETERMINISTIC` flag. This allows SQLite's query planner to:
- Evaluate constant expressions once during query compilation (e.g., `WHERE col = add_numbers(2, 3)` becomes `WHERE col = 5`).
- Cache results inside subqueries, significantly boosting query performance.
