# SQLite Connection State Manager (C/C++)

`sqlite3_conn_state.h` (for Pure C) and `sqlite3_conn_state.hpp` (for C++) provide zero-dependency, **thread-safe, lock-free per-connection state management** for SQLite extensions and embedded servers.

---

## Why do I need this?

- **Shared State (`SqliteExtState`)**: Shared across all connections to the same database file (requires mutex/RW locks).
- **Connection State (`SqliteConnState`)**: Unique to **one single connection (`sqlite3*`)** (runs **100% lock-free during queries**).

If your extension needs to track per-connection query metrics, maintain prepared scratchpad buffers, hold user authentication tokens, or manage session-specific parser contexts, use `SqliteConnState`.

---

## Concurrency & Thread-Safety Model

1. **Parallel Connection Creation & Teardown**:
   - Creating and destroying connections in parallel across worker threads is fully thread-safe.
   - The global pointer registry map is synchronized using an ultra-low-overhead atomic spinlock (`SqliteTinyLock`), eliminating OS mutex context switches and dynamic lazy-initialization overhead.
   - **Zero Lock Contention during State Allocation**: `Entry` allocation and user initialization callbacks (`init_fn`) occur **outside** the lock; the spinlock is held only for nanoseconds during $\mathcal{O}(1)$ map lookup/insertion.
2. **Lock-Free Query Execution**:
   - Because SQLite serializes execution per database connection (`sqlite3*`), query-time state lookups and payload mutations are **100% lock-free** with zero mutex or atomic overhead.
   - Locks are strictly only engaged during connection creation (`init`) and teardown (`remove`, `destructor`). No per-connection state locks (`read_acquire`, `write_acquire`) are needed.
3. **Atomic Reference Counting**:
   - Coordinates multi-UDF registrations using `SqliteAtomic<uint32_t>`, ensuring safe lifecycle management across independent `xDestroy` callbacks without data races.

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

    // Initialize the connection state (thread-safe, non-blocking):
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

Inside your SQL functions, lookups are completely **lock-free** and hit the $\mathcal{O}(1)$ user_data / auxdata fast-path:

```cpp
static void session_query_count_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    MyConnSession* session = SqliteConnState<MyConnSession>::from_context(ctx);
    if (!session) {
        sqlite3_result_error(ctx, "Session state unavailable", -1);
        return;
    }

    // 100% Lock-free mutation (no read_acquire or write_acquire needed):
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

// Generates MyConnSession_init, MyConnSession_from_db, MyConnSession_remove, MyConnSession_destructor:
SQLITE_CONNECTION_STATE(MyConnSession)
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

// Initialize or register with custom setup callback:
void* pApp = SqliteConnState<MyConnSession>::init(db, [](MyConnSession* s) {
    s->session_id = 1001;
    s->query_count = 0;
});
```

---

## Fallible OOM & Lookup Handling (`SqliteResult`)

For applications operating under strict SQLite memory limits (`sqlite3_hard_heap_limit64`):

```cpp
SqliteResult<MyConnSession*> res = SqliteConnState<MyConnSession>::try_get(db);
if (res.is_err()) {
    res.set_sqlite_err(ctx); // Propagates SQLITE_NOTFOUND or error to SQLite query
    return;
}
MyConnSession* session = res.unwrap();
```

---

## Hybrid State (`SqliteHybridState`)

When an SQLite extension requires both **shared per-database state** (e.g. shared index, global cache) and **unique per-connection state** (e.g. session token, local query counter), use `SqliteHybridState<ExtType, ConnType, LockPolicy>` (defined in `sqlite3_hybrid_state.hpp` / `sqlite3_hybrid_state.h`):

```cpp
#include "sqlite3_hybrid_state.hpp"

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
    // Lock-free connection state (0 locks, 1 pointer dereference):
    SessionState *conn = AppHybrid::conn(ctx);
    if (!conn) return;
    conn->local_queries++;

    // Safe RAII write locking strictly on shared extension state:
    {
        AppHybrid::WriteGuard lock(ctx);
        if (!lock) return;
        lock->total_queries++;
    }
}
```

> [!TIP]
> For complete documentation, detailed C/C++ usage guides, and benchmarks on the unified hybrid state subsystem, see [HYBRID_STATE_README.md](./HYBRID_STATE_README.md).

---

## Integration Testing & Go Concurrency Suite

The connection state engine is validated under an intensive multi-database concurrency stress test written in Go (`tests/conn_state/go_loader/concurrency.go` and `lazy_load.go` using `github.com/mattn/go-sqlite3`):

### 1. Test Architecture & Topology
- **3 Independent Databases**: Evaluates concurrency across multiple database files simultaneously (`test_conn_db_0.sqlite`, `test_conn_db_1.sqlite`, `test_conn_db_2.sqlite`).
- **25 Concurrent Connections per DB (75 Physical Connections Total)**: Simulates real-world high-concurrency connection pools.
- **Dual-Barrier Synchronization**:
  - `startBarrier`: Holds all 25 connections per database open simultaneously before executing queries, forcing SQLite to allocate and maintain distinct physical `sqlite3*` connection handles.
  - `doneBarrier`: Prevents any connection from being closed or recycled back into Go's connection pool until all worker goroutines complete their query cycles.

### 2. Verification Invariants
- **Strict Connection Isolation**:
  - Each connection executes 100 iterations.
  - Each iteration calls `test_conn_counter` (+1) and `test_conn_multi_counter` (+10).
  - Given an initial counter of 100, each connection must calculate:
    $$\text{Expected} = 100 + 100 \times (1 + 10) = 1200$$
  - Every one of the 75 connections independently reaches exactly `1200`. Zero state leakage, zero cross-connection pollution, and zero race conditions.
- **Dynamic Runtime Lazy-Loading (`lazy_load.go`)**:
  - Tests dynamic loading via `sqliteConn.LoadExtension` on active connection pools, verifying that runtime state registration and teardown succeed without deadlocks or missed allocations.
- **Zero Memory Leaks**:
  - Validated with Valgrind (`Memcheck`) and Linux GCC `-fsanitize=address,leak` across thousands of allocations and closures (`0 bytes leaked in 0 blocks`).

### Running the Tests
```bash
# In MSYS2 / Windows:
make test-conn-state

# In Linux / WSL (with ASan/LSan):
wsl bash -lc "make test-conn-state"
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

