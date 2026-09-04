# Modern C++ Coroutine Extension Architecture Guide (`example-coro-cpp`)

A modern, freestanding **C++11/C++20** loadable SQLite extension demonstrating the **Type-Safe Tagged Extension-Presence Coroutine Pool Subsystem** ([`include/async/sqlite3_coro_ext_pool.hpp`](file:///c:/msys64/home/dilipvamsi/works/repos/sqlite-ext-core/include/async/sqlite3_coro_ext_pool.hpp)).

---

## 1. Architectural Overview & Value Proposition

In modern C++ SQLite extension development, building high-throughput asynchronous workloads usually requires managing complex threading primitives, thread-local state, and heavy standard library dependencies (`<thread>`, `<future>`, `<functional>`). 

`example-coro-cpp` provides a zero-overhead, freestanding solution with three foundational capabilities:

```
┌───────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                     HOST APPLICATION PROCESS                                      │
│                                                                                                   │
│   ┌───────────────────────────┐   ┌───────────────────────────┐   ┌───────────────────────────┐   │
│   │  Database Connection #1   │   │  Database Connection #2   │   │  Database Connection #3   │   │
│   │    (e.g., Vector DB)      │   │    (e.g., Search DB)      │   │    (e.g., Crypto DB)      │   │
│   └─────────────┬─────────────┘   └─────────────┬─────────────┘   └─────────────┬─────────────┘   │
│                 │                               │                               │                 │
│                 │  Capturing Lambdas            │  Capturing Lambdas            │ wait_all()      │
│                 └───────────────────────┬───────┴───────────────────────────────┘                 │
│                                         │                                                         │
│                                         ▼                                                         │
│         ┌───────────────────────────────────────────────────────────────────────────────┐         │
│         │         SqliteExtCoroPool<CoroCppExtTag> (Template-Isolated Singleton)        │         │
│         │               [Zero-Overhead Static Type-Safe Isolation]                      │         │
│         │                                                                               │         │
│         │   Freestanding Lambda Queue & M:N Work-Stealing Fiber Scheduler               │         │
│         │   ┌───────────────┐ ┌───────────────┐ ┌───────────────┐ ┌───────────────┐     │         │
│         │   │ Lambda [=](){}│ │ Lambda [&](){}│ │ Lambda [=](){}│ │ Lambda [&](){}│ ... │         │
│         │   └───────┬───────┘ └───────┬───────┘ └───────┬───────┘ └───────┬───────┘     │         │
│         │           │                 │                 │                 │             │         │
│         │           ▼                 ▼                 ▼                 ▼             │         │
│         │   ┌───────────────┐ ┌───────────────┐ ┌───────────────┐ ┌───────────────┐     │         │
│         │   │  Worker OS 1  │ │  Worker OS 2  │ │  Worker OS 3  │ │  Worker OS 4  │     │         │
│         │   └───────────────┘ └───────────────┘ └───────────────┘ └───────────────┘     │         │
│         └───────────────────────────────────────────────────────────────────────────────┘         │
└───────────────────────────────────────────────────────────────────────────────────────────────────┘
```

### Key Pillars

1. **Tagged Template Isolation (`SqliteExtCoroPool<Tag>`)**:
   - Each extension defines its own tag struct (e.g. `struct CoroCppExtTag {};`).
   - C++ template instantiation creates an **isolated static worker pool** per tag type at zero runtime cost.
   - Extension A (`SqliteExtCoroPool<VectorTag>`) and Extension B (`SqliteExtCoroPool<SearchTag>`) run in separate, isolated thread pools with zero crosstalk.
2. **Type-Erased Capturing Lambdas without `<functional>`**:
   - Spawns arbitrary stateful C++11 closures (`[&]`, `[=]`, mutable functors) directly into the fiber queue with zero `<functional>` or `<memory>` bloat (`-nostdlib++` safe).
3. **Cooperative Multi-Stage Suspension (`SqliteCoroScheduler::yield()`)**:
   - Fibers execute complex calculations, cooperatively yield CPU control, resume on any available worker thread, and atomically accumulate results.
4. **Automatic RAII Ref-Counted Teardown**:
   - Increments reference counts on database connection open and decrements on `xDestroy`. When the last database closes, worker threads cleanly drain and join.

---

## 2. Template Tag Isolation vs. Traditional Singletons

### Traditional C++ Singletons (Flawed)
A generic `GlobalThreadPool` forces all extensions loaded into the SQLite process to share a single work queue. If a long-running vector search extension saturates the queue, a fast crypto extension stalls.

### Tagged Template Pools (Zero Overhead & Isolated)
```cpp
// Extension 1: Vector Search
struct VectorExtTag {};
using VectorPool = SqliteExtCoroPool<VectorExtTag>;

// Extension 2: Crypto Hashing
struct CryptoExtTag {};
using CryptoPool = SqliteExtCoroPool<CryptoExtTag>;
```

#### Compiler-Level Guarantee:
- `VectorPool` and `CryptoPool` are distinct C++ classes with separate static state (`State s`).
- They instantiate separate OS thread pools, distinct queues, and independent mutexes.
- All connections using `libvector` share `VectorPool`. All connections using `libcrypto` share `CryptoPool`.

---

## 3. Complete Lifecycle & Automatic Teardown (`xDestroy`)

```text
Host App                 DB Connection 1          DB Connection 2        SqliteExtCoroPool<Tag>         M:N Coroutine Workers
   │                            │                        │                          │                          │
   │─── .load libcoro_cpp_ex ──►│                        │                          │                          │
   │                            │─── acquire(4) ───────────────────────────────────►│                          │
   │                            │                        │                          │─── init(4 OS threads) ──►│ (Spawns 4 workers)
   │                            │◄── return scheduler ptr (ref_count = 1) ──────────│                          │
   │                            │─── [Registers C++ UDFs with on_db_disconnect]     │                          │
   │                            │                        │                          │                          │
   │─── .load libcoro_cpp_ex ───────────────────────────►│                          │                          │
   │                            │                        │─── acquire(4) ──────────►│                          │
   │                            │                        │◄── return scheduler ptr (ref_count = 2) ────────────│
   │                            │                        │                          │                          │
   │─── SELECT coro_cpp_spawn()►│                        │                          │                          │
   │                            │─── sqlite_coro_ext_spawn(lambda) ───────────────────────────────────────────►│ (Stage 1 -> Yield -> Stage 2)
   │                            │                        │                          │                          │
   │─── SELECT coro_cpp_spawn()─────────────────────────►│                          │                          │
   │                            │                        │─── sqlite_coro_ext_spawn(lambda) ──────────────────►│ (Shared 4 workers)
   │                            │                        │                          │                          │
   │─── sqlite3_close(DB1) ────►│                        │                          │                          │
   │                            │─── on_db_disconnect() ───────────────────────────►│                          │
   │                            │    [release()]         │                          │ (ref_count: 2 -> 1)      │
   │                            │                        │                          │ [Pool stays active]      │
   │                            │                        │                          │                          │
   │─── sqlite3_close(DB2) ─────────────────────────────►│                          │                          │
   │                            │                        │─── on_db_disconnect() ──►│                          │
   │                            │                        │    [release()]           │ (ref_count: 1 -> 0)      │
   │                            │                        │                          │─── scheduler teardown ──►│ 1. Drain pending lambdas
   │                            │                        │                          │                          │ 2. Join 4 OS threads
   │                            │                        │                          │                          │ 3. Free stack memory
   │                            │                        │                          │◄── pool destroyed ───────│
   │                            │                        │                          │─── free scheduler heap   │ (100% memory freed)
```

---

## 4. Code Anatomy & Implementation Breakdown

### A. Tag Definition & Process-Wide Atomic Metrics
```cpp
#include "sqlite3_ext_creator.hpp"
#include "async/sqlite3_coro_ext_pool.hpp"
#include "sqlite3_atomic.hpp"

// Unique tag type identifying this extension's worker pool
struct CoroCppExtTag {};

### A. Tag Definition & Process-Wide Atomic Metrics
```cpp
#include "sqlite3_ext_creator.hpp"
#include "async/sqlite3_coro_ext_pool.hpp"
#include "sqlite3_atomic.hpp"

// Unique tag type identifying this extension's worker pool
struct CoroCppExtTag {};
using ExampleCoroPool = SqliteExtCoroPool<CoroCppExtTag>;

// Extension-Presence Shared State (process-wide metrics)
static SqliteAtomicInt g_cpp_total_tasks(0);
static SqliteAtomicInt g_cpp_global_sum(0);
```

### B. Capturing Lambda Closure Spawning
```cpp
static void sql_coro_cpp_spawn(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() < 3) {
        ctx.result_error("coro_cpp_spawn requires (db_id, item_id, multiplier)");
        return;
    }

    int db_id = (int)args[0].as_int64();
    int item_id = (int)args[1].as_int64();
    int multiplier = (int)args[2].as_int64();

    // Universal type-safe template spawn into this extension's tagged pool
    bool ok = sqlite_coro_ext_spawn<CoroCppExtTag>([db_id, item_id, multiplier]() {
        (void)db_id;
        // Stage 1: Initial computation for the calling database
        int intermediate = item_id * multiplier;

        // Cooperatively yield CPU control so other database fibers can run
        SqliteCoroScheduler::yield();

        // Stage 2: Second processing phase
        intermediate += 200;

        SqliteCoroScheduler::yield();

        // Stage 3: Atomic accumulation into extension metrics
        g_cpp_global_sum += intermediate;
        g_cpp_total_tasks += 1;
    });

    if (!ok) {
        ctx.result_error("Failed to enqueue closure in extension pool");
        return;
    }

    ctx.result_text("ENQUEUED_IN_CPP_EXTENSION_POOL");
}
```

### C. Synchronous Barrier & Metric Getters
```cpp
// SQL: SELECT coro_cpp_wait();
static void sql_coro_cpp_wait(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    // Blocks calling thread until all queued and executing tasks finish
    ExampleCoroPool::wait_all();
    ctx.result_text("CPP_EXTENSION_POOL_DRAINED");
}

// SQL: SELECT coro_cpp_global_sum();
static void sql_coro_cpp_global_sum(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    ctx.result_int(g_cpp_global_sum.load());
}

// SQL: SELECT coro_cpp_tasks_completed();
static void sql_coro_cpp_tasks_completed(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    ctx.result_int(g_cpp_total_tasks.load());
}

// SQL: SELECT coro_cpp_ref_count();
static void sql_coro_cpp_ref_count(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    ctx.result_int(ExampleCoroPool::ref_count());
}
```

### D. Registration with Disconnection Callback
```cpp
static void on_db_disconnect(void* arg) {
    (void)arg;
    // Decrement database connection reference count for this extension tag
    ExampleCoroPool::release();
}

static int register_coro_cpp_extension(SqliteDatabaseView db) {
    // 1. Acquire reference to the tagged extension presence worker pool (4 workers)
    SqliteCoroScheduler* pool = ExampleCoroPool::acquire(4);
    if (!pool) return SQLITE_NOMEM;

    // 2. Register scalar functions with disconnection callback attached
    sqlite3_create_function_v2(
        db.get(), "coro_cpp_spawn", 3, SQLITE_UTF8, nullptr,
        [](sqlite3_context* c, int argc, sqlite3_value** argv) {
            SqliteContext ctx(c);
            SqliteUdfArgs args(argv, argc);
            sql_coro_cpp_spawn(ctx, args);
        },
        nullptr, nullptr, on_db_disconnect
    );

    SqliteExt::define_scalar(db, "coro_cpp_wait", 0, sql_coro_cpp_wait);
    SqliteExt::define_scalar(db, "coro_cpp_global_sum", 0, sql_coro_cpp_global_sum);
    SqliteExt::define_scalar(db, "coro_cpp_tasks_completed", 0, sql_coro_cpp_tasks_completed);
    SqliteExt::define_scalar(db, "coro_cpp_ref_count", 0, sql_coro_cpp_ref_count);

    return SQLITE_OK;
}

// Named Entrypoints & Default Entrypoint
SQLITE_EXTENSION_ENTRYPOINT(libcoro_cpp_example, db) {
    return register_coro_cpp_extension(db);
}

SQLITE_EXTENSION_ENTRYPOINT(coro_cpp_example, db) {
    return register_coro_cpp_extension(db);
}

SQLITE_DEFAULT_EXTENSION_ENTRYPOINT(db) {
    return register_coro_cpp_extension(db);
}
```

---

## 5. SQL Function Reference

| SQL Function | Arguments | Return Type | Description |
| :--- | :--- | :--- | :--- |
| `coro_cpp_spawn(db_id, item_id, mult)` | `(INT, INT, INT)` | `TEXT` | Asynchronously spawns a capturing lambda closure into the shared worker pool. Returns `'ENQUEUED_IN_CPP_EXTENSION_POOL'`. |
| `coro_cpp_wait()` | None | `TEXT` | Synchronously blocks until all queued and executing tasks in the extension pool complete. Returns `'CPP_EXTENSION_POOL_DRAINED'`. |
| `coro_cpp_global_sum()` | None | `INT` | Returns the process-wide atomic sum computed across all database connections. |
| `coro_cpp_tasks_completed()` | None | `INT` | Returns the total count of fiber tasks completed in the shared extension pool. |
| `coro_cpp_ref_count()` | None | `INT` | Returns the current count of active database connections sharing this extension pool. |

---

## 6. How to Build & Run

### A. MSYS2 / Clang++ / GCC (Linux / macOS / Windows)
```bash
cd example-coro-cpp
make clean && make run
```

### B. MSVC (`cl.exe` / Windows Batch)
```cmd
cd example-coro-cpp
make.bat clean && make.bat
```

### C. Top-Level Repository Target
```bash
make example-coro-cpp
```

---

## 7. Interactive SQL Verification Trace (`example.sql`)

```sql
.load ./build/libcoro_cpp_example

-- 1. Check active database connection references
SELECT coro_cpp_ref_count() AS active_db_connections; -- 1

-- 2. Dispatch batch of capturing lambda fibers from Database 1
SELECT 
    coro_cpp_spawn(1, 1, 10) AS db1_t1,
    coro_cpp_spawn(1, 2, 10) AS db1_t2,
    coro_cpp_spawn(1, 3, 10) AS db1_t3;

-- 3. Dispatch batch of capturing lambda fibers from Database 2
SELECT 
    coro_cpp_spawn(2, 4, 10) AS db2_t4,
    coro_cpp_spawn(2, 5, 10) AS db2_t5;

-- 4. Synchronously drain all 5 capturing lambda tasks
SELECT coro_cpp_wait() AS synchronization_status; -- 'CPP_EXTENSION_POOL_DRAINED'

-- 5. Validate aggregate metrics (intermediate values: 210 + 220 + 230 + 240 + 250 = 1150)
SELECT coro_cpp_tasks_completed() AS total_tasks_completed; -- 5
SELECT coro_cpp_global_sum()      AS global_accumulated_sum;     -- 1150

-- 6. Secondary Batch Enqueue and Verification (Task 6: 60 + 200 = 260)
SELECT coro_cpp_spawn(1, 6, 10) AS db1_t6;
SELECT coro_cpp_wait() AS synchronization_status;
SELECT coro_cpp_tasks_completed() AS total_tasks_completed; -- 6
SELECT coro_cpp_global_sum()      AS global_accumulated_sum;     -- 1410
```
