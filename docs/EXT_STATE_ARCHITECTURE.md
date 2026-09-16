# SQLite Extension State Architecture (Shared & Per-Connection)

Managing state inside a SQLite extension is a notoriously difficult problem because SQLite extensions are loaded **once per process**, but can be used across **multiple concurrent database connections**.

This document outlines the architecture used by:
1. **`sqlite3_ext_state.h` / `sqlite3_ext_state.hpp`**: Thread-safe, garbage-collected, **Per-Database Shared State Registry** backed by DuoSTL string hash maps (`duo_hashmap_t` / `duo::HashMap<duo::String, Entry*>`, $\mathcal{O}(1)$ open addressing).
2. **`sqlite3_conn_state.h` / `sqlite3_conn_state.hpp`**: Lock-free, **Per-Connection Unique State Registry** (`sqlite3*` $\rightarrow$ `T*`) backed by DuoSTL pointer hash maps (`duo_hashmap_t` / `duo::HashMap<sqlite3*, Entry*>`, $\mathcal{O}(1)$).

---

## 1. The Global Static Trap

If you define a global variable in your SQLite extension (e.g., `static int my_counter = 0;`), that state is shared across *every single database* that loads your extension in that process. This breaks database isolation.

Conversely, if you allocate state per-connection (using `sqlite3_set_auxdata` naively), two different connections to the *same* database file won't share the same state, breaking data consistency for things like shared caches, coordinator pools, or sequence generators.

**The Architectural Solution:**
- **Shared State (`SqliteExtState`)**: State is instantiated exactly once per *database file*, shared across all connections to that file, protected by synchronization locks, and cleaned up when the last connection closes.
- **Connection State (`SqliteConnState`)**: State is instantiated exactly once per *connection handle (`sqlite3*`)*, isolated to that single connection, operates completely lock-free, and is destroyed when that specific connection closes.

---

## 2. Per-Database Shared State vs. Per-Connection State

| Dimension | `SqliteExtState` (Shared State) | `SqliteConnState` (Connection State) |
| :--- | :--- | :--- |
| **Scope** | Shared across all connections to the *same* database file | Unique to a single `sqlite3*` connection handle |
| **Registry Key** | Database file path (`const char*`, e.g., `/var/data.db`) | SQLite connection pointer (`sqlite3*`) |
| **Underlying Map** | DuoSTL String Pointer Map (`duo::HashMap<const duo::String*, Entry*>`) | DuoSTL Pointer Hash Map (`duo::HashMap<sqlite3*, Entry*>`) |
| **Concurrency & Locks** | **Required**: Protected by RWLock / TinyLock / Mutex | **Lock-Free**: SQLite connections are single-threaded |
| **Garbage Collection** | Ref-counted via atomics & `sqlite3_set_auxdata` | Destroyed when connection closes via auxdata hook |
| **Best Used For** | Shared caches, pool coordinators, global sequencers | Per-conn query context, user session, parser scratchpad |

---

## 3. The Translation Unit (ODR) Trap

If the internal registry variables were defined using `static` directly inside a header macro, including that header in two different `.c` files would cause the compiler to silently generate two completely independent registries.

To solve this in Pure C, the architecture splits the registry generation into two explicit macros:
1. `SQLITE_EXTENSION_STATE_DECLARE(T)`: Emits `extern` declarations safe for headers.
2. `SQLITE_EXTENSION_STATE_DEFINE(T)`: Emits the actual variables and is strictly enforced to exist in exactly ONE `.c` source file.

In C++, `SqliteExtState<T>` and `SqliteConnState<T>` utilize template static member variables with inline linkage, ensuring exactly one shared instance across all translation units under the C++ One Definition Rule (ODR).

---

## 4. High-Performance $\mathcal{O}(1)$ Hash Maps (DuoSTL)

Both state registries leverage DuoSTL for $\mathcal{O}(1)$ open-addressing hash table lookups with power-of-two capacity doubling:
- **100% SQLite Memory Tracking**: All bucket arrays, string buffers, and hash headers allocate exclusively via `sqlite3_realloc64` and `sqlite3_free` via `duo_alloc.h`.
- **Zero Standard Library Overhead**: Completely operable under `-nostdlib++` with `-fno-exceptions -fno-rtti`.
- **RAII C++ Containers (`stl/duo_hash.hpp`, `stl/duo_linear.hpp`)**: Safe wrappers (`duo::HashMap<K, V>`, `duo::Vector<T>`, `duo::String`) providing intuitive indexing, clean destructor teardown, and copy-prevention semantics.

---

## 5. The 3-Layer Lookup Architecture (The Hot Path)

To achieve extreme performance without lock contention, both state managers use a multi-tiered lookup path:

1. **Layer 1: The O(1) Fast Path (`sqlite3_get_auxdata`)**
   When a scalar function or TVF is called, it first checks SQLite's internal auxdata. This is an immediate pointer dereference. Over 99% of queries hit this fast path, resulting in nanosecond-level lookups with zero mutex locking.
   
2. **Layer 2: The Warm Path (The Global DuoSTL Hash Map)**
   If the fast path misses (e.g., this is the first row of a new query or connection):
   - For `SqliteExtState`: Acquires the global registry lock (`SQLITE_MUTEX_STATIC_APP1`) and looks up the database file path in the DuoSTL string hash map. If found, it increments the reference count, caches the pointer in Layer 1 using `sqlite3_set_auxdata`, and returns.
   - For `SqliteConnState`: Performs an $\mathcal{O}(1)$ pointer lookup in the DuoSTL pointer map without locks, caches in Layer 1, and returns.

3. **Layer 3: The Cold Path (Initialization)**
   If the key does not exist in the hash map, the manager dynamically allocates state via `sqlite_new<T>()` or `sqlite3_malloc`, initializes locks, inserts it into the DuoSTL map, caches in Layer 1, and returns.

---

## 6. Automated Garbage Collection

To prevent memory leaks, extensions bind custom destructors to `sqlite3_set_auxdata`. When a connection closes (or query statement ends):
1. **For `SqliteConnState`**: SQLite calls the auxdata destructor, which removes the `sqlite3*` key from `duo::HashMap` and frees the connection state via `sqlite_delete` / `sqlite3_free`.
2. **For `SqliteExtState`**: The auxdata destructor safely decrements the atomic reference count. If the reference count hits `0`, it knows this was the absolute last connection to the database. It runs the user's `free_fn`, destroys the synchronization locks, removes the entry from the DuoSTL map, and frees the memory.

---

## 7. The "Ghost Removal" Race Condition (Shared State)

A critical race condition exists during garbage collection if two connections to the same database close at the exact same millisecond:
- **Connection A** hits a refcount of 0 and prepares to free the memory.
- **Connection B** (a new connection) sneaks in, finds the registry entry in the hash map, and increments the refcount to 1.
- **Connection A** finishes freeing the memory, leaving Connection B with a dangling pointer (Use-After-Free).

**The Fix (Lock-Free Double-Checked Locking):**
The architecture uses strict double-checked locking, but entirely without mutexes for the reference count. The reference count itself is managed via lock-free atomics (`sqlite_atomic_increment_32`). During destruction, the extension holds the global registry lock and verifies that the reference count is still exactly `0` using `sqlite_atomic_load_32` before committing to the memory deallocation. This perfectly prevents the ghost removal race condition while eliminating all thread-contention on the reference counter.

---

## 8. In-Memory Isolation

In-memory databases (`:memory:`) all share the exact same filepath string (`:memory:`). If the registry relied strictly on filepaths, all in-memory databases would accidentally share the same state.

To prevent this, the architecture intercepts `:memory:` filepaths and dynamically concatenates the raw memory address of the SQLite connection pointer (e.g., `:memory:0x1234abcd`). This guarantees perfect isolation for in-memory databases.

---

## 9. Language Boundaries & Compile-Time Safety

The architecture enforces a strict compile-time boundary between C and C++:
- If a developer attempts to use the pure C `SQLITE_EXTENSION_STATE_DECLARE` macro inside a C++ compiler, it evaluates to a `static_assert(false)` halting compilation.
- This forces developers to use the `SqliteExtState<T>` and `SqliteConnState<T>` C++ templates, preventing memory leaks and object slicing for embedded C++ types.

---

## 10. C++ Memory Lifecycle (`sqlite3_ext_state.hpp` & `sqlite3_conn_state.hpp`)

While C simply allocates structs using `sqlite3_malloc`, the C++ templates must seamlessly support embedding complex C++ objects (like `std::string` or `std::vector`) directly inside the state without causing memory leaks or constructor bypassing:

1. **`sqlite_new`**: During Cold Path initialization, the templates allocate the entire state entry using `sqlite_new<Entry>()`. This forces the C++ compiler to automatically run constructors for all embedded objects (like the state `T` and RAII locks) without pulling in the standard `<new>` header.
2. **`sqlite_delete`**: During Garbage Collection, the templates call `sqlite_delete(entry)`. This guarantees that all embedded C++ objects and locks are safely torn down by standard C++ destructors, completely eliminating the need for manual teardown or a C-style `free_fn` callback.

---

## 11. Fallible Registry Access (`SqliteResult` & `SqliteStatus`)

For strict no-throw error handling and out-of-memory (OOM) resilience without C++ exceptions, both state managers provide fallible methods using the standard Rust-style `SqliteResult<T>` and `SqliteStatus` interfaces:

- **`SqliteExtState<T>::try_get(db)`**: Returns `SqliteResult<T*>`. If the state was not registered during extension loading, returns `SqliteResult<T*>::err(SQLITE_NOTFOUND, ...)`.
- **`SqliteExtState<T>::try_init(db, init_fn)`**: Proactively allocates and registers state for the database connection, returning `SqliteResult<void*>`.
- **`SqliteConnState<T>::try_get(db)`**: Returns `SqliteResult<T*>` for per-connection state.
- **`SqliteConnState<T>::try_init(db, init_fn)`**: Proactively allocates and registers per-connection state, returning `SqliteResult<void*>`.

```cpp
SqliteResult<MyState*> res = SqliteExtState<MyState>::try_get(db);
if (res.is_err()) {
    res.set_sqlite_err(ctx);
    return;
}
MyState* state = res.unwrap();
```

---

## 12. C++17 Baseline & Registration-Owned Lifecycle Reference Counting

### 12.1 Standard C++17 Compiler Requirement
The state subsystem requires a **C++17 baseline** (`-std=c++17` on GCC/Clang, `/std:c++17` on MSVC) while enforcing strict freestanding execution:
- `-nostdlib++`: Drops `libc++`/`libstdc++` dependencies.
- `-fno-exceptions -fno-rtti`: Zero unwind tables and zero RTTI metadata.
- **C++17 Language Features Utilized**:
  - `inline` static template members: Guarantee single-definition ODR safety across multiple translation units without linker duplicate symbol collisions.
  - `if constexpr`: Eliminates dead branch code generation in compile-time lock selection and allocator traits.
  - Structured bindings: Seamlessly unbind hybrid states (`auto [ext, conn] = AppHybrid::from_context(ctx);`).

### 12.2 Elimination of `get_or_create` (The Refcount Leak Trap)
Earlier iterations provided a `get_or_create(db, init_fn)` API. However, in SQLite extensions, state lifecycle is intrinsically bound to SQLite's `sqlite3_create_function_v2` / `sqlite3_create_module_v2` registration and their corresponding `xDestroy` callbacks:
1. If an extension author invoked `get_or_create()` prior to or outside of registration, the internal entry's reference count was initialized without a matching `xDestroy` callback registered in SQLite.
2. During teardown (`sqlite3_close`), SQLite only fired `xDestroy` for the registered functions, leaving the extra reference dangling (`refcount > 0`).
3. Under AddressSanitizer (ASan) and LeakSanitizer (LSan), this produced persistent memory leak reports.

### 12.3 The Registration-Owned Lifecycle Model
To eliminate untracked leaks with 100% mathematical certainty, the architecture enforces a **Registration-Owned Lifecycle**:

```text
┌─────────────────────────────────────────────────────────────────────────────┐
│ 1. Extension Loading Phase (sqlite3_myext_init)                             │
├─────────────────────────────────────────────────────────────────────────────┤
│ • SqliteExt::define_scalar_with_state<AppState, my_func>(db, "fn1")         │
│   └──> Defaults init(db) -> refcount = 1, binds SqliteExtState::destructor  │
│ • SqliteExt::define_scalar_with_state<AppState, other_func>(db, "fn2")      │
│   └──> Retains existing entry -> refcount = 2, binds destructor             │
│                                                                             │
│ • Post-registration state configuration:                                    │
│   AppState* state = SqliteExtState<AppState>::get(db); // refcount UNCHANGED│
│   state->cache_size = 1024;                                                 │
└─────────────────────────────────────────────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ 2. Query Execution Phase (SELECT fn1(), fn2())                              │
├─────────────────────────────────────────────────────────────────────────────┤
│ • Context lookup: ctx.state<AppState>() or SqliteExtState::from_context(ctx)│
│ • Database handle lookup: SqliteExtState::get(db) / try_get(db)             │
│ • Zero reference count modifications: refcount remains 2                    │
└─────────────────────────────────────────────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ 3. Database Teardown Phase (sqlite3_close(db))                              │
├─────────────────────────────────────────────────────────────────────────────┤
│ • SQLite invokes xDestroy for fn1 -> destructor decrements: refcount = 1    │
│ • SQLite invokes xDestroy for fn2 -> destructor decrements: refcount = 0    │
│ • At refcount == 0, double-checked atomic lock frees memory & unlinks entry  │
└─────────────────────────────────────────────────────────────────────────────┘
```

- **`init(db, init_fn)` & `try_init(db, init_fn)`**: Reserved for initialization and registration-time reference binding.
- **`get(db)` & `try_get(db)`**: Non-mutating lookups that never modify reference counts.
- **Symmetrical Balance**: Every `refcount` increment is guaranteed to have exactly one corresponding SQLite `xDestroy` decrement.
