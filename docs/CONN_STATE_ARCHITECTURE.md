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

## 2. Lock-Free Single-Threaded Concurrency

SQLite guarantees that a single database connection (`sqlite3*`) is only active on **one thread at any given moment** during query and statement evaluation:

- **Zero Lock Contention**: Because connection operations are serialized by SQLite's engine, `SqliteConnState` achieves **lock-free** lookups and mutations during query execution.
- **Nanosecond Hot-Path**: Lookups completely bypass mutexes, spinlocks, and atomic compare-and-swap operations on the fast path.
- **Safe Registry Registration**: Mutex synchronization (`SQLITE_MUTEX_STATIC_APP2`) is only engaged during initial registry creation and final connection destruction to safely coordinate the global pointer table.

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

## 4. The 2-Tier Lookup Architecture

To deliver maximum performance, `SqliteConnState` implements a two-tier retrieval pipeline:

```text
       Incoming SQL Function / TVF Call (sqlite3_context *ctx)
                                 |
                                 v
               +-----------------------------------+
               |  Tier 1: Auxdata Fast Path        |
               |  (sqlite3_get_auxdata(ctx, 1))    |
               +-----------------------------------+
                                 |
                     +-----------+-----------+
                     |                       |
                  [ Hit ]                 [ Miss ]
                     |                       |
                     v                       v
              Return cached T*     +--------------------+
                                   | Tier 2: Warm Path  |
                                   | (duo::HashMap get) |
                                   +--------------------+
                                              |
                                   +----------+----------+
                                   |                     |
                                [ Hit ]               [ Miss ]
                                   |                     |
                                   v                     v
                             Cache in AuxData      Cold Path Init:
                             and return T*         allocate Entry,
                                                   duo::HashMap set,
                                                   Cache & return T*
```

1. **Tier 1 (Fast Path - `sqlite3_get_auxdata`)**:
   - Scalar functions and table-valued functions first probe SQLite's internal auxdata on argument slot 1.
   - This performs a direct pointer dereference with **zero hashing and zero mutexes** (99%+ of query iterations).
2. **Tier 2 (Warm Path - DuoSTL Hash Map)**:
   - If auxdata misses (e.g., first invocation in a new statement), it retrieves the `sqlite3*` handle via `sqlite3_context_db_handle(ctx)` and performs an $\mathcal{O}(1)$ lookup in the pointer map.
   - Caches the resolved pointer into Tier 1 via `sqlite3_set_auxdata` for subsequent rows.
3. **Cold Path (Initialization)**:
   - If not yet present, dynamically allocates state via `sqlite_new<Entry>()` or `sqlite3_malloc`, inserts it into DuoSTL map, binds auxdata, and returns.

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

`SqliteConnState<T>` ensures that complex C++ objects (such as `std::string`, `SqliteBuffer`, or custom RAII containers) embedded in the connection state are properly constructed and destructed without pulling in `<new>`:

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

`SqliteHybridState<ExtT, ConnT, LockPolicy>` solves the single-`pApp` / single-`xDestroy` limitation in `sqlite3_create_function_v2`:

```text
┌─────────────────────────────────────────────────────────────┐
│                 sqlite3_create_function_v2                  │
│   pApp = Holder*  ────────────────────► xDestroy = destructor│
└───────────────────────┬─────────────────────────────────────┘
                        │
                        ▼
           ┌────────────────────────┐
           │     Holder Struct      │
           │  ├── ext_raw  (void*)  │──► SqliteExtState<ExtT>::destructor
           │  └── conn_raw (void*)  │──► SqliteConnState<ConnT>::destructor
           └────────────────────────┘
```

### Architectural Highlights:
1. **Unified Holder Lifespan**: `Holder` is allocated via `sqlite3_malloc64` in `init()` and released in `destructor()`.
2. **Dual-Destructor Dispatch**: Decrements the refcount of both `ext_entry` and `conn_entry`, ensuring neither leaks nor double-frees.
3. **Structured Binding Interoperability**: `State` is a standard C++17 aggregate struct allowing `auto [ext, conn] = AppHybrid::from_context(ctx);` without requiring `<tuple>`.
4. **Nested RAII Lock Guards**: `AppHybrid::WriteGuard` and `AppHybrid::ReadGuard` transparently wrap `SqliteExtState` guards, accepting `State` or `ExtT*` directly.

