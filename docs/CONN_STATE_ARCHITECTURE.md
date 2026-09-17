# SQLite Connection State Architecture (Per-Connection State)

In SQLite extensions and embedded database servers, extensions often need to maintain state that is **strictly isolated to a single database connection (`sqlite3*`)** rather than shared across all connections to the database file.

This document outlines the architecture, data structures, and lifecycle mechanics implemented by:
- **`sqlite3_conn_state.h`**: Pure C single-header connection registry macro.
- **`sqlite3_conn_state.hpp`**: C++17 header-only connection state template (`SqliteConnState<T>`).

---

## 1. Why Per-Connection State?

### Shared State (`SqliteExtState`) vs. Connection State (`SqliteConnState`)

| Dimension | `SqliteExtState` (Shared State) | `SqliteConnState` (Connection State) |
| :--- | :--- | :--- |
| **Scope** | Shared across **all** connections to the same `.db` file | Strictly unique to **one** `sqlite3*` handle |
| **Lookup Key** | Database file path (`const char*`) | Raw SQLite connection pointer (`sqlite3*`) |
| **Concurrency Model** | Multi-threaded $\rightarrow$ **Requires locks** (RWLock/TinyLock) | Single-threaded $\rightarrow$ **Lock-free** execution |
| **Underlying Registry** | DuoSTL String Pointer Map (`duo::HashMap<const duo::String*, Entry*>`) | DuoSTL Pointer Hash Map (`duo::HashMap<sqlite3*, Entry*>`) |
| **Typical Use Cases** | Shared caches, pool coordinators, global sequencers | User session IDs, scratchpad buffers, parser state |

---

## 2. Concurrency & Multi-Threading Model

### Lock Scope: Creation and Deletion Only
Locks are **strictly only needed on creation and deletion** (`init`, `remove`, `destructor`, and underlying map lookup/unlinking). Query execution remains **100% lock-free** without per-row mutexes.

- **Zero Lock Contention during State Allocation**: `Entry` allocation (`sqlite_new<Entry>()`) and user initialization callbacks (`init_fn`) occur **outside** the lock. Mutex/spinlock acquisition is strictly confined to $\mathcal{O}(1)$ map lookup and insertion ($\approx 50\text{ns}$), completely preventing thread serialization when multiple connections initialize heavy state (such as embedded Lua runtimes) in parallel.
- **Double-Checked Insertion**: If another worker thread initializes the same connection concurrently, the second entry is cleanly freed via `sqlite_delete` and the existing registered entry is adopted with an incremented reference count.
- **`SqliteTinyLock` Registry Protection**:
  - Replaces heavy OS mutexes with a 1-byte, zero-dependency atomic spinlock (`SqliteTinyLock` from `sqlite3_tiny_lock.hpp`).
  - **Compile-time Zero-Init**: Stored directly in `.bss` with `{0}`, eliminating runtime lazy-initialization (`SQLITE_MUTEX_STATIC_MASTER`).
  - **Per-Type Isolation**: Each instantiated type `SqliteConnState<T>` has its own independent spinlock, preventing cross-type contention.
- **No Per-Connection Mutexes**: Because SQLite's threading architecture serializes query and statement execution per database handle (`sqlite3*`), user state operations require no read/write locks (`read_acquire`/`write_acquire`).

---

## 3. High-Performance $\mathcal{O}(1)$ Pointer Hash Map (DuoSTL)

`SqliteConnState` maps `sqlite3*` connection pointers directly to state entry structures using DuoSTL open-addressing pointer hash maps (`duo::HashMap` / `duo_hashmap_t`):

```text
  +-------------------------------------------------------------+
  |              DuoSTL Pointer Hash Map (Registry)             |
  +-------------------------------------------------------------+
  |  Key (sqlite3*)  |  Value (Entry*)                          |
  +------------------+------------------------------------------+
  |  0x55a120 (db1)  |  --> [ refcount: 2 | ConnState Payload ] |
  |  0x55a980 (db2)  |  --> [ refcount: 1 | ConnState Payload ] |
  |  0x55b410 (db3)  |  --> [ refcount: 3 | ConnState Payload ] |
  +------------------+------------------------------------------+
```

### Memory Tracking
- **100% SQLite Allocators**: The bucket arrays, hash tables, and entry payloads allocate exclusively through `sqlite3_realloc64` and `sqlite3_free`.
- **Zero Standard Library Overhead**: Completely compatible with `-nostdlib++` and `-fno-exceptions -fno-rtti`.

---

## 4. The 2-Tier Fast Lookup Pipeline

To deliver maximum performance and eliminate artificial reference retention, `SqliteConnState` implements an optimized retrieval pipeline:

```text
       Incoming SQL Function / TVF Call (sqlite3_context *ctx)
                                 |
                                 v
               +-----------------------------------+
               |  Tier 1: Direct User Data (Hot)   |
               |  (sqlite3_user_data(ctx))         |
               +-----------------------------------+
                                 |
                     +-----------+-----------+
                     |                       |
                  [ Hit ]                 [ Miss ]
                     |                       |
                     v                       v
              Return cached T*     +--------------------+
              (1 instruction)      | Tier 2: Cold Path  |
              (Zero locks)         | (from_db / get)    |
                                   +--------------------+
                                             |
                                   Look up in DuoSTL Map
                                   under SqliteTinyLock
```

1. **Tier 1 (Hot Path - Direct Function User Data `sqlite3_user_data`)**:
   - For scalar functions, virtual tables, and aggregates registered with `pApp = raw_state`, user data is directly extracted in **1 single CPU instruction** (0 hashing, 0 locks, nanosecond latency).
2. **Tier 2 (Cold Path - Direct Database Handle `from_db`)**:
   - Resolves `sqlite3*` via `sqlite3_context_db_handle(ctx)` and looks up entry in `duo::HashMap` under `SqliteTinyLock`.
   - Accesses state safely without artificial argument auxdata retention (which can fail or leak on 0-argument scalar UDFs).

---

## 5. Multi-Function Reference Counting Lifecycle

A single SQLite connection (`sqlite3* db`) often registers multiple UDFs or TVFs that share the same connection state:

```cpp
// Register Function A
sqlite3_create_function_v2(db, "fn_a", 1, SQLITE_UTF8, state, fn_a, NULL, NULL, 
                           SqliteConnState<MyConnState>::destructor);

// Register Function B
sqlite3_create_function_v2(db, "fn_b", 2, SQLITE_UTF8, state, fn_b, NULL, NULL, 
                           SqliteConnState<MyConnState>::destructor);
```

### The Teardown Challenge
When the connection closes (`sqlite3_close(db)`), SQLite invokes the `xDestroy` callback for **each registered function independently**.

### The Registration-Owned Ref-Count Solution
1. Registration routines (`init(db, init_fn)`, `try_init(db, init_fn)`, `define_with_conn_state`, `define_with_hybrid_state`) initialize or retain the connection entry and increment `refcount` once per registered SQLite hook.
2. Query execution routines (`from_context(ctx)`, `get(db)`, `try_get(db)`) access the state pointer **without modifying refcounts**, completely eliminating reference leak hazards.
3. The legacy `get_or_create` API was explicitly eliminated: invoking `get_or_create` prior to registration caused untracked refcount inflation without corresponding SQLite `xDestroy` bindings, leaking heap entries under AddressSanitizer/LeakSanitizer.
4. When SQLite calls `destructor` for `fn_a`, `refcount` decrements ($2 \rightarrow 1$).
5. When SQLite calls `destructor` for `fn_b`, `refcount` hits $0$.
6. At $0$, the destructor unlinks `db` from the DuoSTL pointer map and invokes `sqlite_delete(entry)` / `sqlite3_free`.
7. This guarantees a mathematically balanced lifecycle: every retained reference maps 1:1 to an SQLite `xDestroy` callback.

---

## 6. C++ Memory Lifecycle (`sqlite3_conn_state.hpp`)

`SqliteConnState<T>` ensures that complex C++ objects (such as `duo::String`, `duo::Bytes`, or custom RAII containers) embedded in the connection state are properly constructed and destructed without pulling in `<new>`:

1. **`sqlite_new<Entry>()`**: Executes in-place construction (`sqlite_construct_at`) for the embedded state `T`.
2. **`sqlite_delete(entry)`**: Calls explicit destructors (`sqlite_destroy_at`) before releasing memory back to SQLite's heap.

---

## 7. Fallible OOM Handling (`SqliteResult` & `SqliteStatus`)

For environments with hard heap limits (`sqlite3_hard_heap_limit64`) and strict no-throw policies:

- **`SqliteConnState<T>::try_get(db)`**: Returns `SqliteResult<T*>`. Returns `SQLITE_NOTFOUND` if not registered.
- **`SqliteConnState<T>::try_init(db, init_fn)`**: Returns `SqliteResult<void*>` for upfront initialization during extension loading.

---

## 8. Hybrid State Architecture (`SqliteHybridState`)

Extensions often require both shared state across connections to the same database file (`SqliteExtState`) and private connection state (`SqliteConnState`).

`SqliteHybridState<ExtT, ConnT, LockPolicy>` (defined in `sqlite3_hybrid_state.hpp` / `sqlite3_hybrid_state.h`) solves the single-`pApp` / single-`xDestroy` limitation in `sqlite3_create_function_v2`:

```text
┌───────────────────────────────────────────────────────────────┐
│                 sqlite3_create_function_v2                    │
│   pApp = Holder*  ────────────────────► xDestroy = destructor │
└───────────────────────┬───────────────────────────────────────┘
                        │
                        ▼
           ┌────────────────────────┐
           │     Holder Struct      │
           │  ├── ext_raw  (ExtT*)  │──► SqliteExtState<ExtT>::destructor
           │  └── conn_raw (ConnT*) │──► SqliteConnState<ConnT>::destructor
           └────────────────────────┘
```

### Architectural Highlights:
1. **Unified Holder Lifespan**: `Holder` is allocated via `sqlite3_malloc64` in `init()` and released in `destructor()`.
2. **Dual-Destructor Dispatch**: Decrements the refcount of both `ext_entry` and `conn_entry`, ensuring neither leaks nor double-frees.
3. **Decoupled State Retrieval**: `AppHybrid::conn(ctx)` and `AppHybrid::conn(db)` resolve the private connection state in 1 CPU instruction with 0 locks and zero touch on shared extension state.
4. **Exclusive Shared-State Locking**: `AppHybrid::WriteGuard` and `AppHybrid::ReadGuard` apply strictly to `ext` state (`AppHybrid::WriteGuard guard(ctx);`), keeping lock scopes cleanly isolated from connection state.

For comprehensive architectural specifications on hybrid state, see [HYBRID_STATE_ARCHITECTURE.md](./HYBRID_STATE_ARCHITECTURE.md).

---

## 9. Verification & Go Concurrency Test Suite

To prove thread-safety, zero lock contention during queries, and absolute connection isolation under real-world multi-threaded SQLite drivers, the test suite includes a Go integration test harness (`tests/conn_state/go_loader`):

### 9.1 Multi-Database Concurrency Topology (`concurrency.go`)
```text
  Database 0 (25 Goroutines / Connections) ──┐
  Database 1 (25 Goroutines / Connections) ──┼──> [Dual-Barrier Sync] ──> 75 Concurrent Workers
  Database 2 (25 Goroutines / Connections) ──┘                                      │
                                                                                    ▼
                                                                        100 Iterations per Conn:
                                                                        - test_conn_counter (+1)
                                                                        - test_conn_multi   (+10)
                                                                                    │
                                                                                    ▼
                                                                 Invariant: Each Conn == 1200
```

1. **Dual-Barrier Thread Pool Enforcement**:
   - **Start Barrier (`startBarrier`)**: Standard database connection pools reuse underlying connections eagerly. To test true connection-state isolation, a `sync.WaitGroup` barrier prevents any queries from executing until all 25 connections per database are opened and acquired. This guarantees 75 distinct physical `sqlite3*` handles running concurrently.
   - **Done Barrier (`doneBarrier`)**: Retains all connections open until every thread finishes its 100 iterations, ensuring no early teardown or handle reuse occurs mid-test.
2. **Isolation & Determinism**:
   - Starting from an initial state counter of 100, each thread performs 100 cycles of scalar (+1) and multi-function (+10) increments.
   - Formula:
     $$\text{Counter} = 100 + 100 \times (1 + 10) = 1200$$
   - Every connection independently concludes with `1200`. If any connection state leaked across threads on the same database, the counter would exceed 1200.
3. **Dynamic Lazy-Loading Validation (`lazy_load.go`)**:
   - Validates that extensions loaded at runtime via `conn.Raw` / `sqliteConn.LoadExtension` register connection states cleanly without requiring driver-level pre-registration.
4. **Sanitizer & Leak Guarantees**:
   - Verified clean under Valgrind Memcheck and GCC/Clang AddressSanitizer/LeakSanitizer with zero memory leaks on connection teardown.

> [!NOTE]
> For testing extensions that combine both connection state and database-shared state simultaneously, see the dedicated Go integration test suite in `tests/hybrid_state/go_loader` and the [HYBRID_STATE_ARCHITECTURE.md](./HYBRID_STATE_ARCHITECTURE.md) guide.

