# SQLite Unified Hybrid State Architecture (`SqliteHybridState`)

This document details the internal memory layout, lifecycle cascades, reference counting mechanics, and concurrency architecture of the Unified Hybrid State subsystem (`SqliteHybridState` in C++ and `SQLITE_HYBRID_STATE` in Pure C).

---

## 1. Architectural Motivation & The Two-Tier Problem

SQLite extension architectures frequently demand two orthogonal scopes of state:

1. **Per-Database Shared State (`SqliteExtState`)**:
   Shared across all concurrent connections targeting the same physical database file (e.g., in-memory shared LRU caches, bloom filters, vector indices, global serial counters). Requires synchronization via read/write locks or mutexes.
2. **Per-Connection Private State (`SqliteConnState`)**:
   Private to a single `sqlite3*` handle (e.g., session identifiers, active transaction staging buffers, query profiling metrics). Serialized by SQLite's single-connection execution model, operating **100% lock-free**.

### The SQLite Engine Constraint

In SQLite's C API, user-defined functions (`sqlite3_create_function_v2`) accept:
- A single `void* pApp` argument.
- A single `void (*xDestroy)(void*)` destructor callback.

```text
┌─────────────────────────────────────────────────────────────┐
│                 sqlite3_create_function_v2                  │
│                                                             │
│   pApp: void* (Only 1 pointer)                              │
│   xDestroy: void (*)(void*) (Only 1 cleanup callback)       │
└─────────────────────────────────────────────────────────────┘
```

Without a unified bridging layer, an extension needing both tiers must either:
- Store only one state in `pApp` and perform dynamic global hash map lookups on every query to fetch the second (sacrificing nanosecond performance).
- Manually allocate ad-hoc pair structs without reference counting, introducing memory leaks or use-after-free bugs when multiple functions are registered or when connections close out-of-order.

`SqliteHybridState` solves this by synthesizing both state lifecycles into a single coherent abstraction.

---

## 2. Memory Layout & The `Holder` Mechanism

### 2.1 Unified Holder Architecture

When `SqliteHybridState::init(db)` (or `Prefix_hybrid_init(db)`) is invoked, it coordinates the initialization of both underlying systems and packages their raw entry handles into an internal `Holder`:

```text
                               ┌──────────────────────────────────────────────┐
                               │                 SQLite Engine                │
                               │  pApp = Holder*  ─────► xDestroy = destructor│
                               └──────────────────────┬───────────────────────┘
                                                      │
                                                      ▼
                                         ┌──────────────────────────┐
                                         │       Holder Struct      │
                                         │  (16 bytes on 64-bit)    │
                                         ├──────────────────────────┤
                                         │ void *ext_raw            │
                                         │ void *conn_raw           │
                                         └───────────┬──────────────┘
                                                     │
                     ┌───────────────────────────────┴───────────────────────────────┐
                     ▼                                                               ▼
       ┌────────────────────────────┐                                  ┌────────────────────────────┐
       │   SqliteExtState Entry     │                                  │   SqliteConnState Entry    │
       ├────────────────────────────┤                                  ├────────────────────────────┤
       │ duo_string_t db_path       │                                  │ sqlite3 *db                │
       │ SqliteAtomic<uint32_t> ref │                                  │ SqliteAtomic<uint32_t> ref │
       │ LockPolicy lock            │                                  │ void (*free_fn)(ConnT*)    │
       │ ExtT state payload         │                                  │ ConnT state payload        │
       └────────────────────────────┘                                  └────────────────────────────┘
```

### 2.2 Holder Memory Layout (64-bit Architecture)

```text
Offset 0x00: void *ext_raw   (8 bytes) ──► Points to SqliteExtState<ExtT>::Entry
Offset 0x08: void *conn_raw  (8 bytes) ──► Points to SqliteConnState<ConnT>::Entry
Total Size:  16 bytes (Allocated exclusively via sqlite3_malloc64)
```

The `Holder` contains zero business logic and zero virtual tables. It serves exclusively as a 16-byte carrier bridging the two independent reference-counted lifecycles.

---

## 3. Reference Counting & Lifecycle Cascades

### 3.1 Multi-Function Registration Cascade

When multiple SQL functions (e.g., `func_a`, `func_b`, `func_c`) are registered on the same database connection using `AppHybrid::init(db)`:

```text
Connection (sqlite3 *db)
  │
  ├─► func_a: Holder 1 ──► ExtEntry (refcount: 3) + ConnEntry (refcount: 3)
  ├─► func_b: Holder 2 ──► ExtEntry (refcount: 3) + ConnEntry (refcount: 3)
  └─► func_c: Holder 3 ──► ExtEntry (refcount: 3) + ConnEntry (refcount: 3)
```

1. **First Function (`func_a`)**:
   - `SqliteExtState::init(db)` creates or retains `ExtEntry` ($\text{ref} = 1$).
   - `SqliteConnState::init(db)` creates `ConnEntry` ($\text{ref} = 1$).
   - Allocates `Holder 1`.
2. **Subsequent Functions (`func_b`, `func_c`)**:
   - `SqliteExtState::init(db)` finds existing `ExtEntry` by database path and atomically increments refcount ($\text{ref} \rightarrow 2, 3$).
   - `SqliteConnState::init(db)` finds existing `ConnEntry` by `sqlite3*` pointer and atomically increments refcount ($\text{ref} \rightarrow 2, 3$).
   - Allocates `Holder 2` and `Holder 3`.

### 3.2 Connection Teardown Cascade

When `sqlite3_close(db)` is called:

```text
sqlite3_close(db)
  │
  ├─► xDestroy(Holder 1) ──► SqliteExtState::destructor(ext_raw)  [ref: 3 -> 2]
  │                     ──► SqliteConnState::destructor(conn_raw) [ref: 3 -> 2]
  │                     ──► sqlite3_free(Holder 1)
  │
  ├─► xDestroy(Holder 2) ──► SqliteExtState::destructor(ext_raw)  [ref: 2 -> 1]
  │                     ──► SqliteConnState::destructor(conn_raw) [ref: 2 -> 1]
  │                     ──► sqlite3_free(Holder 2)
  │
  └─► xDestroy(Holder 3) ──► SqliteExtState::destructor(ext_raw)  [ref: 1 -> 0]
                        ──► SqliteConnState::destructor(conn_raw) [ref: 1 -> 0]
                        ──► sqlite3_free(Holder 3)
```

- When `conn_entry->refcount` reaches 0:
  `SqliteConnState` acquires its internal spinlock, unlinks the entry from `registry_map`, invokes the user `free_fn` (or C++ destructor), and frees `conn_entry`.
- When `ext_entry->refcount` reaches 0:
  If no other active connections to that database file exist, `SqliteExtState` unlinks from the global path map and frees `ext_entry`. If other connections remain open, `ext_entry` stays active.

---

## 4. Dual-Tier Concurrency Architecture

The hybrid framework coordinates three distinct synchronization domains:

```text
┌─────────────────────────────────────────────────────────────────────────────┐
│                          SYNCHRONIZATION DOMAINS                            │
├───────────────────────────────┬─────────────────────────────┬───────────────┤
│ Domain                        │ Mechanism                   │ Contention    │
├───────────────────────────────┼─────────────────────────────┼───────────────┤
│ Global Registries             │ SqliteTinyLock (Spinlock)   │ Minimal       │
│ (Creation & Destruction only) │ Non-blocking alloc outside  │ (< 5 ns)      │
├───────────────────────────────┼─────────────────────────────┼───────────────┤
│ Shared Database State         │ User LockPolicy             │ Application   │
│ (Query execution)             │ (SqliteRwLock / MutexLock)  │ Dependent     │
├───────────────────────────────┼─────────────────────────────┼───────────────┤
│ Connection Private State      │ 100% Lock-Free              │ ZERO          │
│ (Query execution)             │ Single-threaded execution   │ (0 ns)        │
└───────────────────────────────┴─────────────────────────────┴───────────────┘
```

### 4.1 Non-Blocking Allocation Under Spinlock
Both `SqliteExtState` and `SqliteConnState` employ **double-checked insertion with non-blocking outer allocation**:
1. Check global map under `SqliteTinyLock`. If present, retain and return.
2. Release `SqliteTinyLock`.
3. Allocate memory via `sqlite3_malloc64` and execute user initialization callbacks outside any lock.
4. Re-acquire `SqliteTinyLock` to insert. If another worker thread registered concurrently, adopt the existing entry and safely discard the speculative allocation.

---

## 5. State Resolution Pipeline

Resolving hybrid state inside an SQL function occurs via a 2-tier pipeline:

```text
from_context(ctx)
  │
  ├─► Tier 1 (Hot Path): Holder* = (Holder*)sqlite3_user_data(ctx)
  │     │
  │     ├─► [FOUND]
  │     │     ├─► ext  = SqliteExtState::from_ptr(holder->ext_raw)   (Direct pointer arithmetic)
  │     │     ├─► conn = SqliteConnState::from_ptr(holder->conn_raw) (Direct pointer arithmetic)
  │     │     └─► RETURN State{ext, conn}  [LATENCY: ~1.2 ns, 0 LOCKS]
  │     │
  │     └─► [NOT FOUND] (e.g., Virtual Table, Auxiliary Context)
  │           │
  │           ▼
  └─► Tier 2 (Cold Path / Fallback):
        ├─► db = sqlite3_context_db_handle(ctx)
        ├─► AppHybrid::conn(ctx) ──► SqliteConnState::from_db(db) (100% lock-free)
        └─► AppHybrid::ext(ctx)  ──► SqliteExtState::from_db(ctx, db)
```

- **Tier 1 (Hot Path)**: 1 pointer dereference, zero locks, zero hash computations. Executes in $\approx 1.2 \text{ ns}$.
- **Tier 2 (Fallback)**: Used when functions or virtual tables do not have `Holder` directly in `user_data`. Resolves handles through DuoSTL pointer hash maps.

---

## 6. Decoupled Access & RAII Lock Guards

### 6.1 Independent State Retrieval
To avoid false contention and ensure connection state access is never blocked by shared database state, retrieval is decoupled:

- `AppHybrid::conn(ctx)`: Returns `ConnT*` via 1 pointer dereference without touching `ext` or acquiring any locks.
- `AppHybrid::conn(db)`: Returns `ConnT*` directly from the connection handle.
- `AppHybrid::ext(ctx)`: Returns `ExtT*` shared across connections.
- `AppHybrid::ext(db)`: Returns `ExtT*` directly from the database handle.

### 6.2 Nested RAII Lock Guards (Strictly for Ext State)
`AppHybrid::WriteGuard` and `AppHybrid::ReadGuard` apply strictly to `ext` state:

```cpp
// 1. Connection state access (100% lock-free, 0 locks held):
ConnT* conn = AppHybrid::conn(ctx);
conn->local_counter++;

// 2. Explicit Write Lock on Shared Ext Component:
{
    AppHybrid::WriteGuard guard(ctx); // Extracts ext(ctx) and acquires write lock
    // or AppHybrid::WriteGuard guard(ext);
    guard->global_counter++;
} // Guard automatically releases lock here

// 3. Shared Read Lock:
{
    AppHybrid::ReadGuard guard(ctx); // Extracts ext(ctx) and acquires read lock
    int count = guard->global_counter;
}
```

---

## 7. Pure C Macro Architecture (`DEFINE_SQLITE_HYBRID_STATE`)

For pure C extensions, `DEFINE_SQLITE_HYBRID_STATE` generates decoupled zero-overhead routines without C++ compiler dependencies:

```c
#define DEFINE_SQLITE_HYBRID_STATE(ExtStateType, ExtPrefix, ConnStateType, ConnPrefix, HybridPrefix) \
    typedef struct HybridPrefix##_HybridHolder {                                                      \
        void *ext_raw;                                                                                \
        void *conn_raw;                                                                               \
    } HybridPrefix##_HybridHolder;                                                                    \
    ...
```

The macro generates:
- `HybridPrefix_HybridHolder`: Internal carrier packaging `ext_raw` and `conn_raw`.
- `HybridPrefix_hybrid_init(db, ...)`: Initializes both entries and allocates `HybridHolder`.
- `HybridPrefix_hybrid_destructor(p)`: Bridge destructor calling `ExtPrefix_destructor` and `ConnPrefix_destructor`.
- `HybridPrefix_hybrid_conn(ctx)`: Nanosecond lock-free resolution of `ConnStateType*`.
- `HybridPrefix_hybrid_conn_from_db(db)`: Direct resolution of `ConnStateType*` from `sqlite3*`.
- `HybridPrefix_hybrid_ext(ctx)`: Resolution of shared `ExtStateType*`.
- `HybridPrefix_hybrid_ext_from_db(db)`: Direct resolution of shared `ExtStateType*` from `sqlite3*`.
- `HybridPrefix_hybrid_write_acquire / release(ext)`: Lock helpers strictly for shared component.
- `HybridPrefix_hybrid_read_acquire / release(ext)`: Shared read lock helpers strictly for shared component.

---

## 8. Memory Safety & Valgrind Invariants

All memory allocations throughout the hybrid state pipeline strictly obey SQLite memory tracking:
1. `Holder` instances allocate via `sqlite3_malloc64` and release via `sqlite3_free`.
2. All hash table buckets in `duo_hashmap_t` allocate via `sqlite3_realloc64`.
3. No allocations occur through raw `malloc` or `free`.

### Verified Valgrind Invariants
Executed against `tests/hybrid_state`:
- **Heap Blocks Remaining**: Exactly `0 bytes in 0 blocks`.
- **Total Allocations vs Frees**: Exactly equal ($6,004 \text{ allocs} = 6,004 \text{ frees}$).
- **Errors Detected**: 0 errors from 0 contexts.

---

## 9. Integration Verification Architecture (`tests/hybrid_state`)

The integration test suite proves concurrency invariants using two-phase barriers across 3 databases $\times$ 25 connections (75 concurrent connections):

```text
           [ Goroutine 1..25 on DB 0 ]     [ Goroutine 1..25 on DB 1 ]     [ Goroutine 1..25 on DB 2 ]
                        │                               │                               │
                        ▼                               ▼                               ▼
                 [startBarrier]                  [startBarrier]                  [startBarrier]
                 (Hold open 25)                  (Hold open 25)                  (Hold open 25)
                        │                               │                               │
                        ▼                               ▼                               ▼
               [Run 100 Iterations]            [Run 100 Iterations]            [Run 100 Iterations]
               - conn_counter++ (local)        - conn_counter++ (local)        - conn_counter++ (local)
               - ext_counter++ (shared)        - ext_counter++ (shared)        - ext_counter++ (shared)
                        │                               │                               │
                        ▼                               ▼                               ▼
                  [doneBarrier]                   [doneBarrier]                   [doneBarrier]
                 (Prevent reuse)                 (Prevent reuse)                 (Prevent reuse)
                        │                               │                               │
                        ▼                               ▼                               ▼
               [Verify Invariants]             [Verify Invariants]             [Verify Invariants]
               - conn_counter == 200           - conn_counter == 200           - conn_counter == 200
               - ext_counter == 3500           - ext_counter == 3500           - ext_counter == 3500
```

1. **Connection Isolation**: Every connection ends with `conn_counter == 200`, proving that Go connection pool handle reuse cannot corrupt state.
2. **Database Sharing**: Every database ends with `ext_counter == 3500`, proving that all 25 parallel worker threads successfully synchronized without dropping increments.
3. **Database Isolation**: Disparate database files maintain separate counters, proving zero cross-database leakage.
