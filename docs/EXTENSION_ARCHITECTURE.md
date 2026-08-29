# Extension Registration Architecture (`sqlite3_ext_creator.hpp`)

This document details the internal design, ABI trampolines, symbol export mechanisms, dispatch routine resolution, and memory safety models implemented by `sqlite3_ext_creator.hpp`.

---

## 1. Architectural Motivation & Challenges

SQLite loadable extensions are shared objects (`.so`, `.dll`, `.dylib`) dynamically loaded into SQLite processes via `sqlite3_load_extension` or the `.load` CLI command.

### The Technical Challenges:
1. **Dynamic Dispatch Table Routing**: Unlike statically linked host applications, loadable extensions have no direct access to SQLite's C functions. Instead, SQLite passes an API routine table pointer (`sqlite3_api_routines *pApi`) to the initialization function. All SQLite C-API calls inside the extension are transformed into macro dereferences (e.g. `sqlite3_create_function` expands to `sqlite3_api->create_function`). If `SQLITE_EXTENSION_INIT1` and `SQLITE_EXTENSION_INIT2(pApi)` are not structured in the exact required compilation order, undefined symbol crashes occur.
2. **C++ ABI Name Mangling**: By default, C++ compilers mangle symbol names. The dynamic linker looking for `sqlite3_<name>_init` will fail unless the symbol is wrapped in `extern "C"`.
3. **Cross-Platform Visibility Decorators**: Windows DLL linkers require `__declspec(dllexport)` on exported functions. Unix linkers (GCC/Clang) default to exporting all symbols unless compiled with `-fvisibility=hidden`, where `__attribute__((visibility("default")))` is required.
4. **Collision with SQLite Internal Macros**: SQLite reserves `SQLITE_EXTENSION_INIT1`, `SQLITE_EXTENSION_INIT2`, and `SQLITE_EXTENSION_INIT3`. The framework provides `SQLITE_EXTENSION_ENTRYPOINT` to prevent macro namespace collisions.
5. **Zero-Overhead Freestanding Compliance**: The framework must remain strictly `-nostdlib++` compliant without exceptions, RTTI, or standard C++ heap dependencies.

---

## 2. Dynamic Dispatch Trampoline Architecture

When `SQLITE_EXTENSION_ENTRYPOINT(ext_name, db_var)` is declared, it generates a two-tier execution trampoline:

```
+-------------+              +----------------------+             +--------------------+             +--------------------+             +-------------------------+
| SQLite Core |              | Exported Trampoline  |             | Dispatch Table     |             | C++ DB Wrapper     |             | Static User Impl        |
| (sqlite3_   |              | (sqlite3_myext_init) |             | (sqlite3_api)      |             | (SqliteDatabase-   |             | (__sqlite3_ext_         |
| load_ext)   |              |                      |             |                    |             |  View)             |             |  entrypoint_impl_myext) |
+------+------+              +----------+-----------+             +---------+----------+             +---------+----------+             +------------+------------+
       |                                |                                   |                                  |                                     |
       |  1. Call sqlite3_myext_init()  |                                   |                                  |                                     |
       |------------------------------->|                                   |                                  |                                     |
       |                                |  2. SQLITE_EXTENSION_INIT2(pApi)  |                                  |                                     |
       |                                |---------------------------------->|                                  |                                     |
       |                                |                                   |                                  |                                     |
       |                                |  3. Construct SqliteDatabaseView  |                                  |                                     |
       |                                |--------------------------------------------------------------------->|                                     |
       |                                |                                   |                                  |                                     |
       |                                |  4. Invoke User Implementation    |                                  |                                     |
       |                                |----------------------------------------------------------------------------------------------------------->|
       |                                |                                   |                                  |                                     |
       |                                |                                   |                                  |  [Registers UDFs, TVFs, State, etc] |
       |                                |                                   |                                  |                                     |
       |                                |  5. Return SQLITE_OK / Error Code |                                  |                                     |
       |                                |<-----------------------------------------------------------------------------------------------------------|
       |  6. Return status to SQLite    |                                   |                                  |                                     |
       |<-------------------------------|                                   |                                  |                                     |
       |                                |                                   |                                  |                                     |
```

### Static Trampoline Isolation
To prevent symbol pollution and enforce strict encapsulation:
1. The developer's implementation block is compiled as a `static` inline C++ function (`__sqlite3_ext_entrypoint_impl_##ext_name`), making it strictly private to the translation unit.
2. The public entrypoint (`sqlite3_##ext_name##_init`) is exported with `extern "C"` and `SQLITE_EXTENSION_EXPORT`.

---

## 3. Macro Expansion Mechanics

Here is the exact preprocessor expansion for `SQLITE_EXTENSION_ENTRYPOINT(myext, db)`:

```cpp
// 1. Forward-declare the private static C++ implementation
static int __sqlite3_ext_entrypoint_impl_myext(SqliteDatabaseView db);

// 2. Emit the extern "C" dynamic entrypoint required by SQLite's loader
extern "C" SQLITE_EXTENSION_EXPORT int sqlite3_myext_init(
    sqlite3 *db,
    char **pzErrMsg,
    const sqlite3_api_routines *pApi
) {
    // 3a. Initialize global dispatch table
    SQLITE_EXTENSION_INIT2(pApi);

    // 3b. Validate that SQLite provided a valid dispatch table
    if (!pApi) {
        if (pzErrMsg) *pzErrMsg = sqlite3_mprintf("SQLite extension API pointer is NULL");
        return SQLITE_ERROR;
    }

    // 3c. Invoke user implementation with a SqliteDatabaseView wrapper
    return __sqlite3_ext_entrypoint_impl_myext(SqliteDatabaseView(db));
}

// 4. Open definition of user implementation block
static int __sqlite3_ext_entrypoint_impl_myext(SqliteDatabaseView db)
// User code follows immediately:
// {
//     SqliteExt::define_scalar(db, "my_func", 1, my_func_impl);
//     return SQLITE_OK;
// }
```

### Context-Aware Entrypoint (`SQLITE_EXTENSION_ENTRYPOINT_CTX`)
Provides `SqliteExtensionInitContext` with direct access to formatted error strings (`ctx.set_error(...)`) and connection handle:
```cpp
SQLITE_EXTENSION_ENTRYPOINT_CTX(myext, ctx) {
    if (!setup_dependencies()) {
        ctx.set_error("Failed to initialize external resources");
        return SQLITE_ERROR;
    }
    SqliteExt::define_scalar(ctx.db(), "my_func", 1, my_func);
    return SQLITE_OK;
}
```

### Default Entrypoint (`SQLITE_DEFAULT_EXTENSION_ENTRYPOINT`)
Generates the default `int sqlite3_extension_init(...)` exported symbol for dynamic loaders that omit an explicit entrypoint procedure:
```cpp
SQLITE_DEFAULT_EXTENSION_ENTRYPOINT(db) {
    SqliteExt::define_scalar(db, "default_func", 0, default_func);
    return SQLITE_OK;
}
```

### Pure C Macro Suite (`sqlite3_ext_creator.h`)
For extensions implemented in Pure C (C99/C11), dedicated macros provide raw `sqlite3*` dispatching without C++ name-mangling or class wrappers:
- `SQLITE_C_EXTENSION_ENTRYPOINT(ext_name, db)`
- `SQLITE_C_DEFAULT_EXTENSION_ENTRYPOINT(db)`
- `SQLITE_C_EXTENSION_ENTRYPOINT_ERR(ext_name, db, err)`

---

## 4. Context Wrappers & Memory Layout

### `SqliteExtensionInitContext` Memory Layout
When using `SQLITE_EXTENSION_ENTRYPOINT_CTX(ext_name, ctx)`:

```
+------------------------------------+------------------------------------+
|            m_db (8 bytes)          |        m_pzErrMsg (8 bytes)        |
|          sqlite3* pointer          |          char** pointer            |
+------------------------------------+------------------------------------+
<----------------------------- 16 bytes Total ----------------------------->
```

- **Zero Dynamic Allocations**: The context object lives exclusively on the stack.
- **Error Formatting**: `ctx.set_error("...")` uses `sqlite3_mprintf` to guarantee SQLite's internal memory allocator (`sqlite3_free`) can safely free the error string upon failure.

---

## 5. Standard Extension Architectural Patterns

```
                                  sqlite3_ext_creator.hpp
                                              │
      ┌───────────────────────────────────────┼──────────────────────────────────────┐
      │                                       │                                      │
      ▼                                       ▼                                      ▼
┌───────────────────────────┐   ┌───────────────────────────┐  ┌───────────────────────────┐
│ 1. Purely Stateless       │   │ 2. Context-Aware Stateful │  │ 3. Mixed Architecture     │
│                           │   │                           │  │                           │
│ SQLITE_EXTENSION_         │   │ SQLITE_EXTENSION_         │  │ SQLITE_DEFAULT_EXTENSION_ │
│   ENTRYPOINT(ext, db)     │   │   ENTRYPOINT_CTX(ext, ctx)│  │   ENTRYPOINT(db)          │
│                           │   │                           │  │                           │
│ - Pure functions / TVFs   │   │ - Per-connection state    │  │ - Stateless utilities     │
│ - Zero heap state         │   │ - Safe error diagnostics  │  │ - Stateful trackers       │
│ - Deterministic execution │   │ - In-memory DB isolation  │  │ - Default loader fallback │
└───────────────────────────┘   └───────────────────────────┘  └───────────────────────────┘
```

---

## 6. Extension Shared State Architecture (`SqliteExtState<T>`)

Stateful extensions require per-database isolation and thread-safe mutation. `SqliteExtState<T>` provides a 3-tier caching hierarchy:

```
                          +------------------------------------------+
                          | Query Invocation: ext_func(ctx, args)    |
                          +--------------------+---------------------+
                                               |
                                               v
                          +------------------------------------------+
                          | 1. Fast-Path Check:                      |
                          |    sqlite3_user_data(ctx) != NULL?       |
                          +--------------------+---------------------+
                                               |
                         +---------------------+---------------------+
                         | YES                                       | NO
                         v                                           v
         +-------------------------------+         +-----------------------------------+
         | Instant O(1) State Access     |         | 2. Auxdata Cache Check:           |
         | (Direct pApp state pointer)   |         |    sqlite3_get_auxdata(ctx, SLOT)?|
         +-------------------------------+         +-----------------+-----------------+
                                                                     |
                                               +---------------------+-----------------+
                                               | HIT                                   | MISS
                                               v                                       v
                               +-------------------------------+     +-----------------------------------+
                               | O(1) Cached State Pointer     |     | 3. Registry Lookup:               |
                               | (Reused across query rows)    |     |    get_db_path(ctx.db_handle())   |
                               +-------------------------------+     +-----------------+-----------------+
                                               ^                                       |
                                               |                                       v
                                               |                     +-----------------------------------+
                                               |                     | Lock Global Registry Mutex &      |
                                               |                     | Search List (Alloc if new)        |
                                               |                     +-----------------+-----------------+
                                               |                                       |
                                               |                                       v
                                               |                     +-----------------------------------+
                                               |                     | Cache Result in Auxdata Slot      |
                                               +---------------------+ with SQLite Destructor Callback   |
                                                                     +-----------------------------------+
```

### Reference Counting & Garbage Collection:
- **`SqliteExtState<T>::get_or_create(db, init_fn)`**: Retrieves or creates the state without unnecessary reference count inflation.
- **`SqliteExtState<T>::from_context(ctx)`**: Automatically performs the 2-tier resolution (direct `user_data` $\to$ cached auxdata/database lookup).
- **`SqliteExtState<T>::destructor`**: Automatically invoked by SQLite when queries finalize or connections close, safely releasing mutexes and running C++ destructors via `sqlite_delete`.

---

## 7. Compiler & ABI Compatibility

| Platform / Toolchain | Visibility Attribute / Flag | Export Decorator | C++ Standard |
| :--- | :--- | :--- | :--- |
| **Linux (GCC / Clang)** | `-fvisibility=hidden` | `__attribute__((visibility("default")))` | C++11, C++14, C++17, C++20 |
| **Windows (MinGW GCC)** | Default | `__declspec(dllexport)` | C++11, C++14, C++17, C++20 |
| **Windows (MSVC)** | Default | `__declspec(dllexport)` | `/std:c++14`, `/std:c++17`, `/std:c++20` |
| **macOS (Apple Clang)** | Default | `__attribute__((visibility("default")))` | C++11, C++14, C++17, C++20 |

---

## 8. Multi-Extension Dynamic Loading & ASan ODR Handling

When multiple SQLite dynamic extensions (`.so`, `.dll`, `.dylib`) are dynamically loaded via `sqlite3_load_extension()` (`dlopen` / `LoadLibrary`) into the same host process:

1. **Per-Extension Dispatch Table**:
   - Each extension includes `SQLITE_EXTENSION_INIT1`, defining an independent global `const sqlite3_api_routines *sqlite3_api` variable within its own binary image.
2. **AddressSanitizer (ASan) ODR Checker Configuration**:
   - AddressSanitizer's default One Definition Rule (ODR) checker flags duplicate global variable names across distinct `dlopen`ed shared libraries.
   - For host test loaders exercising multiple SQLite extensions concurrently under ASan, `ASAN_OPTIONS=detect_odr_violation=0` is configured to allow separate shared libraries to maintain their own `sqlite3_api` dispatch table without false-positive ODR aborts.

