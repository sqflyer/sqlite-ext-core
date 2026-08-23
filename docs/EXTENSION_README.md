# SQLite C++ Extension Framework (`sqlite3_ext_creator.hpp`)

`sqlite3_ext_creator.hpp` provides an industrial-grade, zero-overhead C++11 framework for developing native, dynamically loadable SQLite extensions (`.so`, `.dll`, `.dylib`). It eliminates the error-prone C boilerplate, symbol visibility decorators, and dispatch table initialization rituals traditionally required to write SQLite extensions, while remaining completely freestanding (`-nostdlib++`, `-fno-exceptions`, `-fno-rtti`).

---

## 1. Overview & Philosophy

SQLite loadable extensions are compiled shared libraries dynamically linked at runtime via SQLite's `sqlite3_load_extension` C-API or the `.load` CLI command. Writing extensions in C++ has historically been fraught with subtle pitfalls:

- **Dispatch Table Initialization**: Unlike host applications that link against SQLite directly (`-lsqlite3`), loadable extensions receive SQLite's API functions via an indirect routine dispatch table (`sqlite3_api`). A single misplaced include or missing initialization macro leads to undefined symbol crashes.
- **ABI Name Mangling**: C++ compilers mangle function names by default, preventing SQLite's dynamic loader from resolving entrypoints without explicit `extern "C"` declarations.
- **Platform Linker Export Decorators**: Dynamic symbols must be exported using compiler-specific attributes (`__declspec(dllexport)` on MSVC/MinGW, `__attribute__((visibility("default")))` on GCC/Clang).
- **Macro Collisions**: SQLite internally reserves `SQLITE_EXTENSION_INIT1`, `SQLITE_EXTENSION_INIT2`, and `SQLITE_EXTENSION_INIT3`. Naming developer macros identically leads to macro shadowing and preprocessor conflicts.
- **Freestanding Memory Constraints**: Standard C++ libraries (`<iostream>`, `<memory>`, `<vector>`) inject heavy runtime dependencies and exceptions.

`sqlite3_ext_creator.hpp` solves all of these challenges seamlessly.

---

## 2. Features Matrix

| Feature | Description |
| :--- | :--- |
| **Zero Boilerplate** | Define full exported extension entrypoints in a single intuitive macro block. |
| **Collision-Free Namespace** | Cleanly named `SQLITE_EXTENSION_ENTRYPOINT` to prevent colliding with SQLite's internal macros. |
| **Cross-Platform Visibility** | Automatic symbol export for Windows (MSVC & MinGW), Linux (GCC & Clang), and macOS. |
| **Modern C++ Lifecycle** | Direct access to `SqliteDatabaseView` and `SqliteContext` without raw pointer management. |
| **Stateful Extensions** | Seamless integration with `SqliteExtState<T>` and `SqliteUdf::define_with_state`. |
| **Freestanding & `-nostdlib++`** | 100% header-only, zero dependencies on `libstdc++`/`libc++`, zero dynamic heap overhead. |
| **Safe Error Reporting** | Formatted diagnostic strings allocated via SQLite's internal string engine (`sqlite3_mprintf`). |
| **Full Ecosystem Support** | Register Scalar UDFs, Aggregates, Table-Valued Functions (TVFs), and Virtual Tables in one place. |

---

## 3. Quickstart Tutorial

### Step 1: Write the Extension (`my_extension.cpp`)

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

// 3. Define the Extension Entrypoint
SQLITE_EXTENSION_ENTRYPOINT(my_extension, db) {
    int rc = SqliteUdf::define(db, "ext_add", 2, ext_add);
    if (rc != SQLITE_OK) return rc;

    rc = SqliteUdf::define(db, "ext_greet", 1, ext_greet);
    if (rc != SQLITE_OK) return rc;

    return SQLITE_OK;
}
```

---

## 4. Compilation Guide

### Linux / Unix (GCC / Clang)
```bash
g++ -shared -fPIC -O2 -std=c++11 -Wall -Wextra \
    -fno-exceptions -fno-rtti -nostdlib++ \
    -I./include -o libmy_extension.so my_extension.cpp
```

### Windows (MSYS2 / MinGW GCC)
```bash
g++ -shared -fPIC -O2 -std=c++11 -Wall -Wextra \
    -fno-exceptions -fno-rtti -nostdlib++ \
    -I./include -o libmy_extension.dll my_extension.cpp
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

### 6.1 `SQLITE_EXTENSION_ENTRYPOINT(ext_name, db_var)`
Defines a named exported entrypoint function `sqlite3_<ext_name>_init`.

```cpp
SQLITE_EXTENSION_ENTRYPOINT(my_analytics, db) {
    // db is an instance of SqliteDatabaseView
    SqliteUdf::define(db, "calc_metric", 1, calc_metric_impl);
    return SQLITE_OK;
}
```

**Parameters**:
- `ext_name`: The unquoted identifier for the extension module (used in `sqlite3_<ext_name>_init`).
- `db_var`: The variable name for the `SqliteDatabaseView` connection parameter.

---

### 6.2 `SQLITE_DEFAULT_EXTENSION_ENTRYPOINT(db_var)`
Defines the generic default fallback entrypoint `sqlite3_extension_init`. SQLite invokes this symbol when `.load <library>` or `sqlite3_load_extension(db, path, NULL, ...)` is called without specifying an explicit procedure name.

```cpp
SQLITE_DEFAULT_EXTENSION_ENTRYPOINT(db) {
    SqliteUdf::define(db, "helper_func", 1, helper_func_impl);
    return SQLITE_OK;
}
```

---

### 6.3 `SQLITE_EXTENSION_ENTRYPOINT_CTX(ext_name, ctx_var)`
Generates an entrypoint receiving `SqliteExtensionInitContext` directly, enabling custom error messaging and diagnostic failure reporting.

```cpp
SQLITE_EXTENSION_ENTRYPOINT_CTX(my_secure_plugin, ctx) {
    if (!verify_license_key()) {
        ctx.set_error("Plugin initialization failed: invalid license credentials");
        return SQLITE_AUTH;
    }
    SqliteUdf::define(ctx.db(), "secure_hash", 1, hash_impl);
    return SQLITE_OK;
}
```

---

### 6.4 `SqliteExtensionInitContext`
Context wrapper providing safe access to the database connection and error assignment.

```cpp
class SqliteExtensionInitContext {
public:
    sqlite3* raw_db() const;                     // Get raw sqlite3* handle
    SqliteDatabaseView db() const;               // Get SqliteDatabaseView wrapper
    operator sqlite3*() const;                   // Implicit conversion to sqlite3*
    operator SqliteDatabaseView() const;         // Implicit conversion to SqliteDatabaseView
    void set_error(const char* message) const;   // Sets error message allocated via sqlite3_mprintf
};
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

## 8. Best Practices & Gotchas

1. **Host Applications vs Extensions**:
   - Host applications linking directly against SQLite (`-lsqlite3`) must define `#define SQLITE_CORE` **before** including `sqlite3_db.hpp` or `sqlite3_statement.hpp`.
   - Loadable extension shared libraries must **not** define `SQLITE_CORE`, and must simply `#include "sqlite3_ext_creator.hpp"`.

2. **Always Use `SqliteContext` for Return Values**:
   - Prefer `ctx.result_int(...)`, `ctx.result_double(...)`, `ctx.result_text(...)`, and `str.result(ctx)` over raw C-APIs to guarantee exception safety and automated memory cleanup.

3. **In-Memory Database Isolation**:
   - Separate `:memory:` database connections are guaranteed perfect isolation by `SqliteExtState` via pointer-keyed virtual path namespaces.

