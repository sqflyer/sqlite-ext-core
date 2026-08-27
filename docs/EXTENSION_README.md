# SQLite Native Extension Framework (`sqlite3_ext_creator.h` / `sqlite3_ext_creator.hpp`)

`sqlite3_ext_creator.h` (Pure C) and `sqlite3_ext_creator.hpp` (C++11) provide an industrial-grade, zero-overhead framework for developing native, dynamically loadable SQLite extensions (`.so`, `.dll`, `.dylib`). They eliminate the error-prone C boilerplate, symbol visibility decorators, and dispatch table initialization rituals traditionally required to write SQLite extensions, while remaining completely freestanding (`-nostdlib++`, `-fno-exceptions`, `-fno-rtti`).

---

## 1. Overview & Philosophy

SQLite loadable extensions are compiled shared libraries dynamically linked at runtime via SQLite's `sqlite3_load_extension` C-API or the `.load` CLI command. Writing extensions has historically been fraught with subtle pitfalls:

- **Dispatch Table Initialization**: Unlike host applications that link against SQLite directly (`-lsqlite3`), loadable extensions receive SQLite's API functions via an indirect routine dispatch table (`sqlite3_api`). A single misplaced include or missing initialization macro leads to undefined symbol crashes.
- **ABI Name Mangling**: C++ compilers mangle function names by default, preventing SQLite's dynamic loader from resolving entrypoints without explicit `extern "C"` declarations.
- **Platform Linker Export Decorators**: Dynamic symbols must be exported using compiler-specific attributes (`__declspec(dllexport)` on MSVC/MinGW, `__attribute__((visibility("default")))` on GCC/Clang).
- **Macro Collisions**: SQLite internally reserves `SQLITE_EXTENSION_INIT1`, `SQLITE_EXTENSION_INIT2`, and `SQLITE_EXTENSION_INIT3`. The framework avoids macro name collisions by providing cleanly namespaced entrypoint macros.
- **Freestanding Memory Constraints**: Standard libraries (`<iostream>`, `<memory>`, `<vector>`) inject heavy runtime dependencies and exceptions.

The extension framework solves all of these challenges seamlessly for both Pure C and C++ development.

---

## 2. Features Matrix

| Feature | C++ (`sqlite3_ext_creator.hpp`) | Pure C (`sqlite3_ext_creator.h`) |
| :--- | :--- | :--- |
| **Header Language** | Modern C++11 (`-nostdlib++`) | Pure C99/C11 (ANSI C) |
| **Named Entrypoint** | `SQLITE_EXTENSION_ENTRYPOINT(ext, db)` | `SQLITE_C_EXTENSION_ENTRYPOINT(ext, db)` |
| **Default Entrypoint** | `SQLITE_DEFAULT_EXTENSION_ENTRYPOINT(db)` | `SQLITE_C_DEFAULT_EXTENSION_ENTRYPOINT(db)` |
| **Error Handling** | `SQLITE_EXTENSION_ENTRYPOINT_CTX(ext, ctx)` | `SQLITE_C_EXTENSION_ENTRYPOINT_ERR(ext, db, err)` |
| **Database Parameter** | `SqliteDatabaseView` or `SqliteExtensionInitContext` | Raw `sqlite3*` handle |
| **Symbol Visibility** | Automatic `dllexport` / `visibility("default")` | Automatic `dllexport` / `visibility("default")` |
| **Ecosystem Support** | Scalar UDFs, Aggregates, TVFs, Virtual Tables | Scalar UDFs, Aggregates, Virtual Tables |
| **Shared State (Per-DB)** | `SqliteExtState<T>` (RAII guards) | `SQLITE_EXTENSION_STATE_DECLARE` / `DEFINE` |
| **Async Coroutine Pool (Cross-DB)** | `SqliteExtCoroPool<Tag>` / `sqlite_coro_ext_spawn` | `sqlite3_coro_ext_pool_*` (`sqlite3_coro_ext_pool.h`) |
| **Pool Tag Model** | Template Type Tag Monomorphization | Zero-collision Static Memory Pointer (`SQLITE_EXT_TAG`) |

---

## 3. Quickstart Tutorials

### 3.1 C++ Extension Quickstart (`my_extension.cpp`)

```cpp
#include "sqlite3_ext_creator.hpp"

// 1. Define a Scalar UDF using SqliteContext
static void ext_add(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() < 2) {
        ctx.result_error("ext_add requires 2 numeric arguments");
        return;
    }
    sqlite3_int64 a = args[0].as_int64();
    sqlite3_int64 b = args[1].as_int64();
    ctx.result_int64(a + b);
}

// 2. Define a String UDF with SqliteStringOwned
static void ext_greet(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() < 1) {
        ctx.result_error("ext_greet requires a name argument");
        return;
    }
    SqliteStringOwned res(ctx.get());
    res.appendall("Hello, ");
    SqliteStringView name = args[0].as_text();
    res.append(name.data(), name.length());
    res.appendall("!");
    res.result(ctx);
}

// 3. Define the Extension Entrypoint: sqlite3_my_extension_init
SQLITE_EXTENSION_ENTRYPOINT(my_extension, db) {
    int rc = SqliteExt::define_scalar(db, "ext_add", 2, ext_add);
    if (rc != SQLITE_OK) return rc;

    rc = SqliteExt::define_scalar(db, "ext_greet", 1, ext_greet);
    if (rc != SQLITE_OK) return rc;

    return SQLITE_OK;
}
```

*For complete turnkey examples, see [`example-cpp/README.md`](../example-cpp/README.md).*

---

### 3.2 Pure C Extension Quickstart (`my_c_extension.c`)

```c
#include "sqlite3_ext_creator.h"

// 1. Scalar Function
static void c_add_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    if (argc < 2) {
        sqlite3_result_error(ctx, "c_add requires 2 arguments", -1);
        return;
    }
    sqlite3_int64 a = sqlite3_value_int64(argv[0]);
    sqlite3_int64 b = sqlite3_value_int64(argv[1]);
    sqlite3_result_int64(ctx, a + b);
}

// 2. Define Entrypoint: sqlite3_my_c_ext_init
SQLITE_C_EXTENSION_ENTRYPOINT(my_c_ext, db) {
    return sqlite3_create_function(
        db, "c_add", 2, 
        SQLITE_UTF8 | SQLITE_DETERMINISTIC, 
        NULL, c_add_func, NULL, NULL
    );
}
```

*For complete Pure C state management examples, see [`example-c/README.md`](../example-c/README.md).*

---

## 4. Compilation Guide

### Linux / Unix (GCC / Clang)
```bash
# C++ Extension
g++ -shared -fPIC -O2 -std=c++11 -Wall -Wextra \
    -fno-exceptions -fno-rtti -nostdlib++ \
    -I./include -o libmy_extension.so my_extension.cpp

# Pure C Extension
gcc -shared -fPIC -O2 -std=c99 -Wall -Wextra \
    -I./include -o libmy_c_ext.so my_c_extension.c
```

### Windows (MSYS2 / MinGW GCC)
```bash
# C++ Extension
g++ -shared -fPIC -O2 -std=c++11 -Wall -Wextra \
    -fno-exceptions -fno-rtti -nostdlib++ \
    -I./include -o libmy_extension.dll my_extension.cpp

# Pure C Extension
gcc -shared -fPIC -O2 -std=c99 -Wall -Wextra \
    -I./include -o libmy_c_ext.dll my_c_extension.c
```

### Windows (MSVC `cl.exe`)
```cmd
cl /LD /O2 /std:c++14 /GR- /EHsc- /W4 /I.\include my_extension.cpp /link /OUT:my_extension.dll
```

### macOS (Clang)
```bash
clang++ -dynamiclib -fPIC -O2 -std=c++11 -Wall -Wextra \
    -fno-exceptions -fno-rtti -nostdlib++ \
    -I./include -o libmy_extension.dylib my_extension.cpp
```

---

## 5. Loading and Using the Extension

### SQLite Command-Line Interface (CLI)
```sql
-- Load using explicit entrypoint name
.load ./libmy_extension.so sqlite3_my_extension_init

-- Or load with default name resolution
.load ./libmy_extension.so

-- Execute functions
SELECT ext_add(15, 27);         -- Returns 42
SELECT ext_greet('World');      -- Returns 'Hello, World!'
```

### Host C++ Application Loading
`sqlite3_db.hpp` provides type-safe helpers directly on `SqliteDatabaseView` and `SqliteDatabaseOwned`:

```cpp
#define SQLITE_CORE
#include "sqlite3_db.hpp"
#include <assert.h>
#include <stdio.h>

int main() {
    SqliteDatabaseOwned db(":memory:");

    // 1. Enable extension loading capability on this connection
    db.enable_load_extension(true);

    // 2. Load the compiled extension shared library
    char* err_msg = nullptr;
    int rc = db.load_extension("./libmy_extension.so", "sqlite3_my_extension_init", &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to load extension: %s\n", err_msg ? err_msg : "unknown");
        if (err_msg) sqlite3_free(err_msg);
        return 1;
    }

    // 3. Execute queries using the extension's UDFs
    SqliteStatement stmt = db.prepare("SELECT ext_add(10, 20);");
    assert(stmt.next());
    printf("Result: %lld\n", stmt.column_int64(0)); // 30
    return 0;
}
```

### Python (`sqlite3`)
```python
import sqlite3

con = sqlite3.connect(":memory:")
con.enable_load_extension(True)
con.load_extension("./libmy_extension.so")

cur = con.cursor()
cur.execute("SELECT ext_add(100, 250);")
print(cur.fetchone()[0]) # 350
```

---

## 6. Comprehensive API Reference

### 6.1 C++ Macros (`sqlite3_ext_creator.hpp`)

#### `SQLITE_EXTENSION_ENTRYPOINT(ext_name, db_var)`
Defines a named exported entrypoint function `sqlite3_<ext_name>_init`.

```cpp
SQLITE_EXTENSION_ENTRYPOINT(my_analytics, db) {
    // db is an instance of SqliteDatabaseView
    SqliteExt::define_scalar(db, "calc_metric", 1, calc_metric_impl);
    return SQLITE_OK;
}
```

#### `SQLITE_DEFAULT_EXTENSION_ENTRYPOINT(db_var)`
Defines the generic default fallback entrypoint `sqlite3_extension_init`. SQLite invokes this symbol when `.load <library>` or `sqlite3_load_extension(db, path, NULL, ...)` is called without specifying an explicit procedure name.

```cpp
SQLITE_DEFAULT_EXTENSION_ENTRYPOINT(db) {
    SqliteExt::define_scalar(db, "helper_func", 1, helper_func_impl);
    return SQLITE_OK;
}
```

#### `SQLITE_EXTENSION_ENTRYPOINT_CTX(ext_name, ctx_var)`
Generates an entrypoint receiving `SqliteExtensionInitContext` directly, enabling custom error messaging and diagnostic failure reporting.

```cpp
SQLITE_EXTENSION_ENTRYPOINT_CTX(my_secure_plugin, ctx) {
    if (!verify_license_key()) {
        ctx.set_error("Plugin initialization failed: invalid license credentials");
        return SQLITE_AUTH;
    }
    SqliteExt::define_scalar(ctx.db(), "secure_hash", 1, hash_impl);
    return SQLITE_OK;
}
```

---

### 6.2 Pure C Macros (`sqlite3_ext_creator.h`)

#### `SQLITE_C_EXTENSION_ENTRYPOINT(ext_name, db_var)`
Defines a named Pure C exported entrypoint function `sqlite3_<ext_name>_init` receiving `sqlite3 *db_var`.

```c
SQLITE_C_EXTENSION_ENTRYPOINT(my_c_ext, db) {
    return sqlite3_create_function(db, "c_func", 1, SQLITE_UTF8, NULL, c_func, NULL, NULL);
}
```

#### `SQLITE_C_DEFAULT_EXTENSION_ENTRYPOINT(db_var)`
Defines the default Pure C `sqlite3_extension_init` export receiving `sqlite3 *db_var`.

```c
SQLITE_C_DEFAULT_EXTENSION_ENTRYPOINT(db) {
    return sqlite3_create_function(db, "default_c_func", 0, SQLITE_UTF8, NULL, fn, NULL, NULL);
}
```

#### `SQLITE_C_EXTENSION_ENTRYPOINT_ERR(ext_name, db_var, err_var)`
Generates a named Pure C entrypoint receiving `sqlite3 *db_var` and `char **err_var` for custom `sqlite3_mprintf` error reporting.

```c
SQLITE_C_EXTENSION_ENTRYPOINT_ERR(my_c_ext, db, pzErr) {
    if (init_subsystems() != 0) {
        *pzErr = sqlite3_mprintf("Failed to allocate C subsystems");
        return SQLITE_ERROR;
    }
    return SQLITE_OK;
}
```

---

## 7. Extension Architecture Patterns

### 7.1 Pattern 1: Purely Stateless Extension
Stateless extensions implement pure deterministic functions, aggregations, TVFs, and virtual tables that require no shared memory:

```cpp
#include "sqlite3_ext_creator.hpp"

// Entrypoint: sqlite3_stateless_ext_init
SQLITE_EXTENSION_ENTRYPOINT(stateless_ext, db) {
    SqliteExt::define_scalar(db, "stateless_add", 2, stateless_add);
    SqliteExt::define_aggregate<StatelessSumSq>(db, "stateless_sum_sq", 1);
    SqliteExt::define_tvf<StatelessRangeIterator>(db, "stateless_range");
    SqliteExt::define_vtab<StatelessEchoTable>(db, "stateless_echo");
    return SQLITE_OK;
}
```

---

### 7.2 Pattern 2: Context-Aware Stateful Extension
Stateful extensions maintain in-memory counters, cache pools, or session registries bound to each SQLite database connection:

```cpp
#include "sqlite3_ext_creator.hpp"

struct SessionState {
    int counter;
    char session_tag[64];
};

// Entrypoint with direct SqliteExtensionInitContext: sqlite3_stateful_ext_init
SQLITE_EXTENSION_ENTRYPOINT_CTX(stateful_ext, ctx) {
    SqliteDatabaseView db = ctx.db();

    // 1. Initialize per-connection state
    SqliteExt::init_state<SessionState>(db, [](SessionState* s) {
        s->counter = 100;
        const char* tag = "PROD";
        memcpy(s->session_tag, tag, strlen(tag) + 1);
    });

    // 2. Register stateful components across all 4 subsystems
    SqliteExt::define_scalar_with_state<SessionState, stateful_inc>(db, "stateful_inc", 0);
    SqliteExt::define_aggregate_with_state<SessionState, StatefulConcat>(db, "stateful_concat", 1);
    SqliteExt::define_tvf_with_state<SessionState, StatefulMetricsTvf>(db, "stateful_metrics");
    SqliteExt::define_vtab_with_state<SessionState, StatefulCacheTable>(db, "stateful_cache");
    return SQLITE_OK;
}
```

---

### 7.3 Pattern 3: Mixed Extension with Default Entrypoint
Mixed extensions combine fast stateless utilities and shared stateful engines, exporting SQLite's default `sqlite3_extension_init` entrypoint:

```cpp
#include "sqlite3_ext_creator.hpp"

struct AuditState {
    int total_queries;
};

// Default entrypoint loaded via .load <file> or sqlite3_load_extension(db, file, NULL, ...)
SQLITE_DEFAULT_EXTENSION_ENTRYPOINT(db) {
    // Initialize state
    SqliteExt::init_state<AuditState>(db, [](AuditState* s) { s->total_queries = 0; });

    // Stateless helpers
    SqliteExt::define_scalar(db, "mixed_multiply", 2, mixed_multiply);
    SqliteExt::define_tvf<MixedIotaIterator>(db, "mixed_iota");

    // Stateful trackers
    SqliteExt::define_scalar_with_state<AuditState, mixed_audit>(db, "mixed_audit", 0);
    SqliteExt::define_aggregate_with_state<AuditState, MixedWeightedAvg>(db, "mixed_weighted_avg", 2);
    return SQLITE_OK;
}
```

---

### 7.4 Pattern 4: Extension-Presence Shared Coroutine Worker Pool

Unlike per-connection state which is destroyed when a database closes, extension-presence pools maintain a shared background thread/fiber pool that lives across multiple active database connections within the host process:

#### C++11 / C++20 Tagged Coroutine Pool
```cpp
#include "sqlite3_ext_creator.hpp"
#include "async/sqlite3_coro_ext_pool.hpp"

// Unique static type tag identifying this extension's worker pool
struct MyVectorSearchTag {};
using VectorSearchPool = SqliteExtCoroPool<MyVectorSearchTag>;

static void sql_async_search(SqliteContext ctx, SqliteUdfArgs args) {
    int query_id = args[0].as_int();

    // Spawn non-blocking fiber on shared extension worker pool
    sqlite_coro_ext_spawn<MyVectorSearchTag>([query_id]() {
        // Heavy computation or index traversal
        SqliteCoroScheduler::yield(); // Cooperatively yield CPU to other queries
    });

    ctx.result_text("ENQUEUED");
}

SQLITE_EXTENSION_ENTRYPOINT(vector_ext, db) {
    // 1. Acquire reference to extension pool (spawns 4 background workers on first DB connection)
    VectorSearchPool::acquire(4);

    // 2. Register disconnect callback to decrement ref-count when DB closes
    sqlite3_create_function_v2(
        db.get(), "async_search", 1, SQLITE_UTF8, nullptr,
        [](sqlite3_context* c, int argc, sqlite3_value** argv) {
            SqliteContext ctx(c);
            SqliteUdfArgs args(argv, argc);
            sql_async_search(ctx, args);
        },
        nullptr, nullptr, [](void*) { VectorSearchPool::release(); }
    );

    return SQLITE_OK;
}
```

#### Pure C Tagged Coroutine Pool
```c
#include "sqlite3_ext_creator.h"
#include "async/sqlite3_coro_ext_pool.h"

// Static tag address guaranteeing zero symbol collision across dynamic libraries
static const int MyCExtTag = 0;

static void on_db_disconnect(void* arg) {
    (void)arg;
    sqlite3_coro_ext_pool_release(SQLITE_EXT_TAG(MyCExtTag));
}

SQLITE_C_EXTENSION_ENTRYPOINT(my_coro_c_ext, db) {
    // Acquire shared pool with 4 worker threads
    sqlite3_coro_pool_t* pool = sqlite3_coro_ext_pool_acquire(SQLITE_EXT_TAG(MyCExtTag), 4);
    if (!pool) return SQLITE_NOMEM;

    sqlite3_create_function_v2(
        db, "coro_spawn", 1, SQLITE_UTF8, NULL,
        sql_coro_spawn_func, NULL, NULL, on_db_disconnect
    );
    return SQLITE_OK;
}
```

---

## 8. Extension-Presence Shared Coroutine Worker Pools

For comprehensive architectural design, systems invariants, and microbenchmarks, see:
- [`docs/CORO_EXT_POOL_README.md`](CORO_EXT_POOL_README.md) - User guide and use cases (vector search, crypto hashing, cloud sync TVF, chunked compression).
- [`docs/CORO_EXT_POOL_ARCHITECTURE.md`](CORO_EXT_POOL_ARCHITECTURE.md) - Systems architecture, memory layout, and lock hierarchies.
- [`example-coro-cpp/`](../example-coro-cpp) - Turnkey C++ tagged coroutine extension example.
- [`example-coro-c/`](../example-coro-c) - Turnkey Pure C tagged coroutine extension example.

### 8.1 Physical Architecture & Zero-Collision Address Tagging

```
+-----------------------------------------------------------------------------------+
|                           HOST OS PROCESS ADDRESS SPACE                           |
|                                                                                   |
|  +-----------------------------+         +-----------------------------+          |
|  |   Database Connection #1    |         |   Database Connection #2    |          |
|  |     (sqlite3* handle 1)     |         |     (sqlite3* handle 2)     |          |
|  +--------------+--------------+         +--------------+--------------+          |
|                 |                                       |                         |
|                 | .load ./my_vector_ext.so              | .load ./my_vector_ext.so|
|                 v                                       v                         |
|  +-----------------------------------------------------------------------------+  |
|  |                       EXTENSION PRESENCE WORKER POOL                        |  |
|  |   Tag: &MyVectorSearchTag (Static Zero-Collision Virtual Memory Address)    |  |
|  |   Atomic Reference Count: 2 (Auto-Freed when all DBs disconnect)            |  |
|  +-----------------------------------------------------------------------------+  |
|         |                     |                     |                     |       |
|         v                     v                     v                     v       |
|  +--------------+    +--------------+    +--------------+    +--------------+     |
|  | Worker OS #1 |    | Worker OS #2 |    | Worker OS #3 |    | Worker OS #4 |     |
|  | (Win Fiber/  |    | (Win Fiber/  |    | (Win Fiber/  |    | (Win Fiber/  |     |
|  |  ucontext_t) |    |  ucontext_t) |    |  ucontext_t) |    |  ucontext_t) |     |
|  +--------------+    +--------------+    +--------------+    +--------------+     |
+-----------------------------------------------------------------------------------+
```

### 8.2 API Overview

| Function / Method | Purpose | Language |
| :--- | :--- | :--- |
| `sqlite3_coro_ext_pool_acquire(tag, workers)` | Acquires/creates a tagged worker pool; increments ref count. | C |
| `sqlite3_coro_ext_pool_get(tag)` | Looks up active worker pool pointer without modifying ref count. | C |
| `sqlite3_coro_ext_pool_release(tag)` | Decrements ref count; destroys and unlinks pool when count hits 0. | C |
| `sqlite3_coro_ext_pool_wait(tag)` | Synchronously drains and waits for all active fibers in the tagged pool. | C |
| `sqlite3_coro_ext_pool_ref_count(tag)` | Returns active database connection count. | C |
| `sqlite3_coro_ext_pool_shutdown_all()` | Shuts down and frees all extension pools process-wide. | C |
| `SqliteExtCoroPool<Tag>::acquire(workers)` | Type-safe C++ template acquisition. | C++11 |
| `SqliteExtCoroPool<Tag>::get()` | Type-safe C++ template lookup. | C++11 |
| `SqliteExtCoroPool<Tag>::release()` | Type-safe C++ template release. | C++11 |
| `sqlite_coro_ext_spawn<Tag>(closure)` | Enqueues stateful capturing lambda into tagged extension pool. | C++11 |

---

## 9. Best Practices & Gotchas

1. **Host Applications vs Extensions**:
   - Host applications linking directly against SQLite (`-lsqlite3`) must define `#define SQLITE_CORE` **before** including `sqlite3_db.hpp` or `sqlite3_statement.hpp`.
   - Loadable extension shared libraries must **not** define `SQLITE_CORE`, and must simply include `sqlite3_ext_creator.hpp` (C++) or `sqlite3_ext_creator.h` (C).

2. **Always Use `SqliteContext` for Return Values**:
   - Prefer `ctx.result_int(...)`, `ctx.result_double(...)`, `ctx.result_text(...)`, and `str.result(ctx)` over raw C-APIs to guarantee exception safety and automated memory cleanup.

3. **In-Memory Database Isolation vs Shared Presence**:
   - Use `SqliteExtState<T>` when data must be strictly isolated to a single database connection.
   - Use `SqliteExtCoroPool<Tag>` when worker threads and async task execution queues should be shared across connections to conserve CPU/memory resources.
   - Separate `:memory:` database connections are guaranteed perfect isolation by `SqliteExtState` via pointer-keyed virtual path namespaces.
