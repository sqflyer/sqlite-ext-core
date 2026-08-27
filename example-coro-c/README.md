# Pure C Coroutine Extension Architecture Guide (`example-coro-c`)

A complete, production-grade SQLite loadable extension written in **Pure C99/C11** demonstrating the **Freestanding Extension-Presence Coroutine Pool Subsystem** ([`include/async/sqlite3_coro_ext_pool.h`](file:///c:/msys64/home/dilipvamsi/works/repos/sqlite-ext-core/include/async/sqlite3_coro_ext_pool.h)).

---

## 1. Architectural Overview & Value Proposition

Traditional asynchronous SQLite extensions typically suffer from one of two design flaws:
1. **Per-Connection Thread Spawning**: Creating 4 background OS threads for every SQLite database connection. When 20 connections open within a web server or backend service, 80 OS threads are spawned, leading to massive memory overhead and kernel scheduling thrashing.
2. **Process-Wide Global Name Collisions**: Keying background thread pools by raw string names (e.g. `"my_pool"` or `"vector"`). If two third-party extensions pick the same string, they silently corrupt each other's execution queues.

`example-coro-c` demonstrates the **Extension-Presence Coroutine Pool Architecture**:

```
┌─────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                   HOST APPLICATION PROCESS                                      │
│                                                                                                 │
│   ┌────────────────────────┐      ┌────────────────────────┐      ┌─────────────────────────┐   │
│   │ Database Connection #1 │      │ Database Connection #2 │      │ Database Connection #3  │   │
│   │  (e.g., Read Replica)  │      │  (e.g., Write Master)  │      │   (e.g., Analytics DB)  │   │
│   └───────────┬────────────┘      └───────────┬────────────┘      └────────────┬────────────┘   │
│               │                               │                                │                │
│               │  coro_c_spawn(1, ...)         │  coro_c_spawn(2, ...)          │ coro_c_wait()  │
│               └───────────────────────┬───────┴────────────────────────────────┘                │
│                                       │                                                         │
│                                       ▼                                                         │
│         ┌─────────────────────────────────────────────────────────────────────────────┐         │
│         │               EXTENSION PRESENCE COROUTINE POOL (Tag: CoroCExtTag)          │         │
│         │               [100% Collision-Proof Virtual Address Keying]                 │         │
│         │                                                                             │         │
│         │   Work-Stealing / M:N Cooperative Coroutine Task Queue                      │         │
│         │   ┌──────────────┐ ┌──────────────┐ ┌──────────────┐ ┌──────────────┐       │         │
│         │   │ Fiber Task A │ │ Fiber Task B │ │ Fiber Task C │ │ Fiber Task D │ ...   │         │
│         │   └──────┬───────┘ └──────┬───────┘ └──────┬───────┘ └──────┬───────┘       │         │
│         │          │                │                │                │               │         │
│         │          ▼                ▼                ▼                ▼               │         │
│         │   ┌──────────────┐ ┌──────────────┐ ┌──────────────┐ ┌──────────────┐       │         │
│         │   │   Worker 1   │ │   Worker 2   │ │   Worker 3   │ │   Worker 4   │ (OS)  │         │
│         │   └──────────────┘ └──────────────┘ └──────────────┘ └──────────────┘       │         │
│         └─────────────────────────────────────────────────────────────────────────────┘         │
└─────────────────────────────────────────────────────────────────────────────────────────────────┘
```

### Key Pillars

- **Zero-Collision Tagged Pointer Keying**: Extension pools are keyed by the static memory address of a translation-unit token (`SQLITE_EXT_TAG_DECLARE(CoroCExtTag)`). The OS dynamic loader assigns every `.dll` / `.so` a unique virtual address range, guaranteeing mathematical and physical zero collision.
- **Process-Wide Multi-DB Sharing**: When 10 database connections load `libcoro_c_example`, they share **one dedicated 4-worker pool** rather than creating 40 threads.
- **M:N Cooperative Multitasking**: Thousands of lightweight user-space fibers execute across $N$ OS worker threads. Fibers yield cooperatively via `sqlite3_coro_pool_yield()` across computational stages.
- **Deterministic Atomic Lifecycle (`xDestroy`)**: Tracks active database connection references. When the final database connection closes, SQLite automatically fires the destructor callback, cleanly draining and destroying the worker pool with zero memory leaks.

---

## 2. Zero-Collision Memory Model Deep-Dive

### Why String-Based Discovery Fails
In traditional C plugin registries:
```c
// DANGEROUS: String name collision
sqlite3_coro_ext_pool_acquire("analytics_ext", 4);
```
If Extension A and Extension B both choose `"analytics_ext"`, they collide in the registry and share a pool unexpectedly.

### How Tagged Pointer Keying Solves It
```c
// SAFE & GUARANTEED 100% UNIQUE BY OS VIRTUAL MEMORY MANAGER:
SQLITE_EXT_TAG_DECLARE(CoroCExtTag);

sqlite3_coro_pool_t* pool = sqlite3_coro_ext_pool_acquire(SQLITE_EXT_TAG(CoroCExtTag), 4);
```

#### Under the Hood:
1. `SQLITE_EXT_TAG_DECLARE(CoroCExtTag)` expands to:
   ```c
   static const char __sqlite3_ext_tag_CoroCExtTag = 0;
   ```
2. The compiler places `__sqlite3_ext_tag_CoroCExtTag` into the static data section of `libcoro_c_example.dll`.
3. When the OS loads the shared library, it assigns a unique base address (e.g. `0x7FFA89301040`).
4. The registry searches by pointer address (`curr->tag == tag`), which is an **$O(1)$ integer comparison** that is 100% collision-proof.

---

## 3. Complete Lifecycle & Automatic Teardown (`xDestroy`)

The extension hooks into SQLite's native `sqlite3_create_function_v2` destruction callback:

```text
Host App                 DB Connection 1          DB Connection 2        Extension Registry (Tag)      M:N Coroutine Pool
   │                            │                        │                          │                          │
   │─── .load libcoro_c_example ───►                     │                          │                          │
   │                            │─── acquire(Tag, 4) ──────────────────────────────►│                          │
   │                            │                        │                          │─── init(4 workers) ─────►│ (Spawns 4 threads)
   │                            │◄── return pool ptr (ref_count = 1) ───────────────│                          │
   │                            │─── [Registers UDFs with on_db_disconnect]         │                          │
   │                            │                        │                          │                          │
   │─── .load libcoro_c_example ────────────────────────►│                          │                          │
   │                            │                        │─── acquire(Tag, 4) ─────►│                          │
   │                            │                        │◄── return pool ptr (ref_count = 2) ─────────────────│
   │                            │                        │                          │                          │
   │─── SELECT coro_c_spawn() ─►│                        │                          │                          │
   │                            │─── enqueue fiber ───────────────────────────────────────────────────────────►│ (Stage 1 -> Yield -> Stage 2)
   │                            │                        │                          │                          │
   │─── SELECT coro_c_spawn() ──────────────────────────►│                          │                          │
   │                            │                        │─── enqueue fiber ──────────────────────────────────►│ (Shared 4 workers)
   │                            │                        │                          │                          │
   │─── sqlite3_close(DB1) ────►│                        │                          │                          │
   │                            │─── on_db_disconnect() ───────────────────────────►│                          │
   │                            │    [release(Tag)]      │                          │ (ref_count: 2 -> 1)      │
   │                            │                        │                          │ [Pool stays active]      │
   │                            │                        │                          │                          │
   │─── sqlite3_close(DB2) ─────────────────────────────►│                          │                          │
   │                            │                        │─── on_db_disconnect() ──►│                          │
   │                            │                        │    [release(Tag)]        │ (ref_count: 1 -> 0)      │
   │                            │                        │                          │─── pool_destroy() ──────►│ 1. Drain pending fibers
   │                            │                        │                          │                          │ 2. Join 4 OS threads
   │                            │                        │                          │                          │ 3. Free stack buffers
   │                            │                        │                          │◄── pool destroyed ───────│
   │                            │                        │                          │─── free(registry_node)   │ (100% memory freed)
```

---

## 4. Code Anatomy & Implementation Breakdown

### A. Static Tag Declaration (`example.c`)
```c
#include "sqlite3_ext_creator.h"
#include "async/sqlite3_coro_ext_pool.h"
#include "sqlite3_atomic.h"

// Declare static token for 100% collision-proof isolation
SQLITE_EXT_TAG_DECLARE(CoroCExtTag);
```

### B. Cooperative Multi-Stage Fiber Routine
```c
typedef struct {
    int db_id;
    int item_id;
    int multiplier;
} SharedTaskPayload;

static void extension_worker_fiber(void* arg) {
    SharedTaskPayload* p = (SharedTaskPayload*)arg;

    // Stage 1: Initial local calculation
    int intermediate = p->item_id * p->multiplier;

    // Yield control to let fibers from other databases make progress
    sqlite3_coro_pool_yield();

    // Stage 2: Secondary computation phase
    intermediate += 100;

    // Yield control again
    sqlite3_coro_pool_yield();

    // Stage 3: Atomic accumulation into extension metrics
    sqlite3_atomic_fetch_add(&g_global_sum, intermediate);
    sqlite3_atomic_fetch_add(&g_total_tasks, 1);

    // Free dynamic payload
    sqlite3_free(p);
}
```

### C. SQL UDF Dispatch (`coro_c_spawn`)
```c
static void sql_coro_c_spawn(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    int db_id = sqlite3_value_int(argv[0]);
    int item_id = sqlite3_value_int(argv[1]);
    int multiplier = sqlite3_value_int(argv[2]);

    SharedTaskPayload* p = (SharedTaskPayload*)sqlite3_malloc64(sizeof(SharedTaskPayload));
    p->db_id = db_id;
    p->item_id = item_id;
    p->multiplier = multiplier;

    // Acquire pool (returns shared singleton pointer)
    sqlite3_coro_pool_t* pool = sqlite3_coro_ext_pool_acquire(SQLITE_EXT_TAG(CoroCExtTag), 4);

    // Enqueue fiber into worker pool
    sqlite3_coro_pool_spawn(pool, extension_worker_fiber, p, 0);

    sqlite3_result_text(ctx, "ENQUEUED_IN_EXTENSION_POOL", -1, SQLITE_STATIC);
}
```

### D. Registration with Disconnection Callback
```c
static void on_db_disconnect(void* arg) {
    (void)arg;
    sqlite3_coro_ext_pool_release(SQLITE_EXT_TAG(CoroCExtTag));
}

static int register_coro_c_extension(sqlite3* db) {
    // Acquire pool and increment reference count
    sqlite3_coro_pool_t* pool = sqlite3_coro_ext_pool_acquire(SQLITE_EXT_TAG(CoroCExtTag), 4);
    if (!pool) return SQLITE_NOMEM;

    // Attach `on_db_disconnect` as 9th parameter (xDestroy)
    sqlite3_create_function_v2(db, "coro_c_spawn", 3, SQLITE_UTF8, NULL,
                               sql_coro_c_spawn, NULL, NULL, on_db_disconnect);
    sqlite3_create_function(db, "coro_c_wait", 0, SQLITE_UTF8, NULL,
                            sql_coro_c_wait, NULL, NULL);
    sqlite3_create_function(db, "coro_c_global_sum", 0, SQLITE_UTF8, NULL,
                            sql_coro_c_global_sum, NULL, NULL);
    sqlite3_create_function(db, "coro_c_tasks_completed", 0, SQLITE_UTF8, NULL,
                            sql_coro_c_tasks_completed, NULL, NULL);
    sqlite3_create_function(db, "coro_c_ref_count", 0, SQLITE_UTF8, NULL,
                            sql_coro_c_ref_count, NULL, NULL);

    return SQLITE_OK;
}
```

---

## 5. SQL Function Reference

| SQL Function | Arguments | Return Type | Description |
| :--- | :--- | :--- | :--- |
| `coro_c_spawn(db_id, item_id, mult)` | `(INT, INT, INT)` | `TEXT` | Asynchronously spawns a 3-stage cooperative fiber into the shared worker pool. Returns `'ENQUEUED_IN_EXTENSION_POOL'`. |
| `coro_c_wait()` | None | `TEXT` | Synchronously blocks the calling thread until all queued and executing fibers in the extension pool complete. Returns `'EXTENSION_POOL_DRAINED'`. |
| `coro_c_global_sum()` | None | `INT` | Returns the process-wide atomic sum computed across all database connections. |
| `coro_c_tasks_completed()` | None | `INT` | Returns the total count of fiber tasks completed in the shared extension pool. |
| `coro_c_ref_count()` | None | `INT` | Returns the current count of active database connections sharing this extension pool. |

---

## 6. How to Build & Run

### A. MSYS2 / Clang / GCC (Linux / macOS / Windows)
```bash
cd example-coro-c
make clean && make run
```

### B. MSVC (`cl.exe` / Windows Batch)
```cmd
cd example-coro-c
make.bat clean && make.bat
```

### C. Top-Level Repository Target
```bash
make example-coro-c
```

---

## 7. Interactive SQL Verification Trace (`example.sql`)

```sql
.load ./build/libcoro_c_example

-- 1. Inspect active database connections sharing pool
SELECT coro_c_ref_count() AS active_db_connections; -- 1

-- 2. Dispatch batch of fibers from Database 1
SELECT coro_c_spawn(1, 10, 1) AS db1_t1,
       coro_c_spawn(1, 20, 1) AS db1_t2,
       coro_c_spawn(1, 30, 1) AS db1_t3,
       coro_c_spawn(1, 40, 1) AS db1_t4,
       coro_c_spawn(1, 50, 1) AS db1_t5;

-- 3. Dispatch batch of fibers from Database 2
SELECT coro_c_spawn(2, 60, 1) AS db2_t6,
       coro_c_spawn(2, 70, 1) AS db2_t7,
       coro_c_spawn(2, 80, 1) AS db2_t8,
       coro_c_spawn(2, 90, 1) AS db2_t9,
       coro_c_spawn(2, 100, 1) AS db2_t10;

-- 4. Synchronously drain all 10 multi-stage fibers
SELECT coro_c_wait() AS synchronization_status; -- 'EXTENSION_POOL_DRAINED'

-- 5. Validate aggregate metrics
SELECT coro_c_tasks_completed() AS total_tasks_completed; -- 10
SELECT coro_c_global_sum() AS global_accumulated_sum;     -- 1550
```
