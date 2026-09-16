# SQLite Connection State Manager (C/C++)

`sqlite3_conn_state.h` (for Pure C) and `sqlite3_conn_state.hpp` (for C++) provide zero-dependency, **lock-free, per-connection state management** for SQLite extensions and embedded servers.

---

## Why do I need this?

- **Shared State (`SqliteExtState`)**: Shared across all connections to the same database file (requires mutex/RW locks).
- **Connection State (`SqliteConnState`)**: Unique to **one single connection (`sqlite3*`)** (runs **100% lock-free**).

If your extension needs to track per-connection query metrics, maintain prepared scratchpad buffers, hold user authentication tokens, or manage session-specific parser contexts, use `SqliteConnState`.

---

## Quickstart (C++)

### 1. Define your Connection Context

```cpp
#include "sqlite3ext.h"
#include "sqlite3_conn_state.hpp"

struct MyConnSession {
    int query_count = 0;
    int session_id = 0;
    char user_name[32];
};
```

### 2. Initialize in your Extension Hook

```cpp
int sqlite3_myext_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi) {
    SQLITE_EXTENSION_INIT2(pApi);

    // Initialize the connection state:
    void* raw_state = SqliteConnState<MyConnSession>::init(db);

    // Register UDFs with the connection state destructor:
    return sqlite3_create_function_v2(
        db, "session_query_count", 0, SQLITE_UTF8,
        raw_state,
        session_query_count_func,
        NULL, NULL,
        SqliteConnState<MyConnSession>::destructor // Automatically frees on connection close!
    );
}
```

### 3. Access State Inside UDFs / TVFs

Inside your SQL functions, lookups are completely **lock-free** and hit the $\mathcal{O}(1)$ auxdata fast-path:

```cpp
static void session_query_count_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    MyConnSession* session = SqliteConnState<MyConnSession>::from_context(ctx);
    if (!session) {
        sqlite3_result_error(ctx, "Session state unavailable", -1);
        return;
    }

    // Lock-free mutation:
    session->query_count++;

    sqlite3_result_int(ctx, session->query_count);
}
```

---

## Quickstart (Pure C)

### 1. Declare and Define the Connection State Macro

```c
#include "sqlite3ext.h"
#include "sqlite3_conn_state.h"

typedef struct {
    int query_count;
    int session_id;
} MyConnSession;

// Generates MyConnSession_init, MyConnSession_get, MyConnSession_destructor:
SQLITE_CONNECTION_STATE(MyConnSession, MyConnSession)
```

### 2. Initialize and Register in Pure C

```c
int sqlite3_myext_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi) {
    SQLITE_EXTENSION_INIT2(pApi);

    void* raw_state = MyConnSession_init(db, NULL, NULL);

    return sqlite3_create_function_v2(
        db, "session_query_count", 0, SQLITE_UTF8,
        raw_state,
        session_query_count_func,
        NULL, NULL,
        MyConnSession_destructor
    );
}

static void session_query_count_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    MyConnSession* session = MyConnSession_from_context(ctx);
    if (!session) return;

    session->query_count++;
    sqlite3_result_int(ctx, session->query_count);
}
```

---

## Direct Database Handle Access

You can also retrieve or mutate connection state directly from a `sqlite3*` pointer outside of UDF contexts:

```cpp
// Look up state directly by connection handle:
MyConnSession* session = SqliteConnState<MyConnSession>::get(db);
if (session) {
    session->session_id = 42;
}

// Proactive creation / initialization:
MyConnSession* session = SqliteConnState<MyConnSession>::get_or_create(db, [](MyConnSession* s) {
    s->session_id = 1001;
    s->query_count = 0;
});
```

---

## Fallible OOM Handling (`SqliteResult`)

For applications operating under strict SQLite memory limits (`sqlite3_hard_heap_limit64`):

```cpp
SqliteResult<MyConnSession*> res = SqliteConnState<MyConnSession>::try_get_or_create(db, ctx);
if (res.is_err()) {
    res.set_sqlite_err(ctx); // Propagates SQLITE_NOMEM to SQLite query
    return;
}
MyConnSession* session = res.unwrap();
```

---

## Hybrid State (`SqliteHybridState`)

When an SQLite extension requires both **shared per-database state** (e.g. shared index, global cache) and **unique per-connection state** (e.g. session token, local query counter), use `SqliteHybridState<ExtType, ConnType, LockPolicy>`:

```cpp
#include "sqlite3_conn_state.hpp"

struct GlobalCache {
    int total_queries = 0;
};

struct SessionState {
    int local_queries = 0;
};

using AppHybrid = SqliteHybridState<GlobalCache, SessionState>;

// 1. Extension Init: Single unified registration and single destructor
void* pApp = AppHybrid::init(db, init_global, init_session);
sqlite3_create_function_v2(db, "hybrid_fn", 0, SQLITE_UTF8, pApp, hybrid_fn, NULL, NULL, AppHybrid::destructor);

// 2. Inside UDF:
static void hybrid_fn(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    auto state = AppHybrid::from_context(ctx);

    // Lock-free connection state:
    state.conn->local_queries++;

    // Safe RAII write locking on shared state:
    {
        AppHybrid::WriteGuard lock(state);
        lock->total_queries++;
    }

    // Structured binding support:
    // auto [ext, conn] = AppHybrid::from_context(ctx);
}
```

---

## Key Features

- **Lock-Free Execution**: Takes advantage of SQLite's single-threaded connection model to bypass all mutex and spinlock overhead during queries.
- **High-Performance $\mathcal{O}(1)$ DuoSTL Pointer Maps**: Backed by DuoSTL open-addressing pointer hash maps (`duo::HashMap` / `duo_hashmap_t`).
- **Hybrid State Architecture**: Unifies `SqliteExtState` and `SqliteConnState` into a single `Holder` with unified `xDestroy` bridging and RAII locking.
- **100% SQLite Memory Tracking**: All memory allocations route through `sqlite3_realloc64` and `sqlite3_free`.
- **Multi-Function Ref Counting**: Prevents double-free and use-after-free bugs when multiple UDFs/TVFs share the same connection state.
- **Zero Standard Library Overhead**: Fully functional under `-nostdlib++` with `-fno-exceptions -fno-rtti`.

For internal implementation details, see [CONN_STATE_ARCHITECTURE.md](./CONN_STATE_ARCHITECTURE.md).

