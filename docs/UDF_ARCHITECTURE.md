# C++ UDF Builder Architecture (`sqlite3_udf.hpp`)

This document details the internal design, trampoline proxy mechanisms, state-binding lifecycles, and memory safety models implemented by `sqlite3_udf.hpp`.

---

## 1. Architectural Objectives

1. **Zero Dynamic Allocation**: Parameter wrapping and function invocation must not trigger any `malloc`, `new`, or standard library heap traffic.
2. **Bounds-Checked Parameter Access**: Protect against segmentation faults and memory corruptions when user functions access `argv`.
3. **Strict Freestanding Portability**: Operate completely without `<functional>`, `<tuple>`, RTTI, or exceptions (`-nostdlib++ -fno-exceptions -fno-rtti`).
4. **Natural Value Type Integration**: Seamlessly interoperate with `SqliteContext`, `SqliteValueView`, `SqliteStringOwned`, and `SqliteBlobOwned`.
5. **Multi-Function Shared State**: Allow multiple UDFs on the same database connection to share and mutate the same state instance with thread-safe locking and automated SQLite garbage collection.

---

## 2. Stateless Function Trampoline Proxy

In standard SQLite C extensions, registering a user-defined function requires invoking `sqlite3_create_function_v2` with a raw C callback:
```c
void (*xFunc)(sqlite3_context*, int, sqlite3_value**);
```

Passing C++ member functions or higher-level signatures directly is prevented by C linkage rules. `SqliteUdf` solves this with a zero-cost static **Trampoline Proxy**:

```
+------------------+         +-------------------------------+         +-----------------------+
|  SQLite Core     | ------> |  SqliteUdf::scalar_proxy()    | ------> |  User C++ Function    |
|  (sqlite3_step)  |         |  (C Callback Trampoline)      |         |  (or stateless lambda)|
+------------------+         +-------------------------------+         +-----------------------+
                                           |
                              - Reads `sqlite3_user_data(ctx)`
                              - Wraps `SqliteContext(ctx)`
                              - Constructs `SqliteUdfArgs(argc, argv)`
                              - Dispatches to target C++ function
```

### Stateless Registration Lifecycle
1. When `SqliteUdf::define(db, name, num_args, func)` is called, the C++ function pointer `func` is cast to `void*` and passed directly as the `pApp` (user data) parameter in `sqlite3_create_function_v2`.
2. When SQL queries invoke the function, SQLite passes this pointer back to `scalar_proxy` via `sqlite3_user_data(ctx)`.
3. `scalar_proxy` wraps the raw context in `SqliteContext`, constructs `SqliteUdfArgs`, and invokes `func(ctx, args)`.

---

## 3. Stateful Function Architecture: Shared State Across Multiple UDFs

When multiple functions need to share and mutate the same state struct (e.g. `AppState`), `SqliteUdf` combines **`SqliteExtState<T>`** with **Compile-Time Template Proxies**:

```
                          +-------------------------------------------------------------+
                          | Extension Entrypoint / Database Init:                       |
                          | SqliteExtState<AppState>::get_or_create(db, init_fn)        |
                          +------------------------------+------------------------------+
                                                         | (Allocates single shared Entry)
                                                         v
                                          +-------------------------------+
                                          | Shared State Instance (Entry) |
                                          | (Retained refcount per func)  |
                                          +---------------+---------------+
                                                          |
                 +----------------------------------------+----------------------------------------+
                 | Passed as pApp to func 1                                        | Passed as pApp to func 2
                 v                                                                 v
+------------------------------------+                            +------------------------------------+
| SQLite Registration:               |                            | SQLite Registration:               |
| define_with_state<AppState, func1> |                            | define_with_state<AppState, func2> |
| (xDestroy = destructor)            |                            | (xDestroy = destructor)            |
+-----------------+------------------+                            +-----------------+------------------+
                  |                                                                 |
                  | Query: SELECT func1()                                           | Query: SELECT func2()
                  v                                                                 v
+------------------------------------+                            +------------------------------------+
| template_proxy_context<func1>      |                            | template_proxy_context<func2>      |
| -> sqlite3_user_data(ctx) = Entry* |                            | -> sqlite3_user_data(ctx) = Entry* |
+-----------------+------------------+                            +-----------------+------------------+
                  |                                                                 |
                  +-------------------------------+---------------------------------+
                                                  |
                                                  v
                               +------------------------------------+
                               | Both functions read & mutate the   |
                               | EXACT SAME Shared State Instance!  |
                               +------------------------------------+
```

### Key Architectural Advantages:
1. **Zero-Overhead $O(1)$ Direct State Access**:
   Inside each function, `SqliteExtState<AppState>::from_context(ctx)` or `ctx.state<AppState>()` directly reads `sqlite3_user_data(ctx)` in **1 CPU instruction**. No hash table lookups, no string path searches, and zero heap allocations.
2. **Automated SQLite Garbage Collection (`xDestroy`)**:
   Each `define_with_state` passes `SqliteExtState<State>::destructor` as SQLite's `xDestroy` callback. When the database closes or functions are unregistered, SQLite automatically decrements the reference count and frees the state when no functions remain.
3. **Cross-Function Thread Safety**:
   State access is coordinated using `SqliteExtState<State>::ReadGuard` (shared read lock) and `SqliteExtState<State>::WriteGuard` (exclusive write lock), backed by platform-native fast locks (Windows SRWLock, POSIX `pthread_rwlock_t`).

---

## 4. Zero-Allocation Argument Wrapper (`SqliteUdfArgs`)

The `SqliteUdfArgs` class is a lightweight 16-byte stack structure on 64-bit platforms:

```
+------------------------------------+------------------------------------+
|            m_argc (4 bytes)        |          (4 bytes padding)         |
|         integer argument count     |                                    |
+------------------------------------+------------------------------------+
|            m_argv (8 bytes)                                             |
|         sqlite3_value** array pointer                                   |
+-------------------------------------------------------------------------+
<----------------------------- 16 bytes Total ---------------------------->
```

### Bounds Safety Mechanics
`operator[](int index)` guarantees memory safety by intercepting out-of-bounds queries:

```cpp
inline SqliteValueView operator[](int index) const {
    if (index < 0 || index >= m_argc) {
        return SqliteValueView(nullptr);
    }
    return SqliteValueView(m_argv[index]);
}
```

If an out-of-bounds index is queried (e.g. `args[-1]`, `args[999]`), it returns a `SqliteValueView(nullptr)` whose `type()` evaluates to `SQLITE_NULL`, preventing invalid memory dereferencing or crashes.

---

## 5. `SqliteContext` Zero-Cost Abstraction

`SqliteContext` is a zero-overhead, 8-byte stack wrapper around `sqlite3_context*`:

```cpp
class SqliteContext {
private:
    sqlite3_context* m_ctx; // 8 bytes on 64-bit
public:
    inline sqlite3_context* get() const { return m_ctx; }
    inline operator sqlite3_context*() const { return m_ctx; }
    inline sqlite3* db_handle() const { return sqlite3_context_db_handle(m_ctx); }

    template <typename State>
    inline State* state() const {
        return SqliteExtState<State>::from_context(m_ctx);
    }
    // ... result and error helpers ...
};
```

All methods are marked `inline` and compile down to direct SQLite C-API calls with zero indirection overhead.
