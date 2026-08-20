# SQLite Extension Shared State Architecture

Managing state inside a SQLite extension is a notoriously difficult problem because SQLite extensions are loaded **once per process**, but can be used across **multiple concurrent database connections**.

This document outlines the architecture used by `sqlite3_ext_state.h` to solve this problem by implementing a thread-safe, garbage-collected, per-database shared state registry.

## 1. The Global Static Trap
If you define a global variable in your SQLite extension (e.g., `static int my_counter = 0;`), that state is shared across *every single database* that loads your extension in that process. This breaks database isolation.

Conversely, if you allocate state per-connection (using `sqlite3_set_auxdata` naively), two different connections to the *same* database file won't share the same state, breaking data consistency for things like caches or connection pools.

**The Solution:** State must be instantiated exactly once per *database file*, shared across all connections to that file, and cleaned up when the last connection closes.

## 2. The 3-Layer Lookup (The Hot Path)
To achieve extreme performance without lock contention, the state manager uses a 3-layer caching architecture:

1. **Layer 1: The O(1) Fast Path (`sqlite3_get_auxdata`)**
   When a scalar function is called, it first checks SQLite's internal auxdata. This is an immediate pointer dereference. Over 99% of queries hit this fast path, resulting in nanosecond-level lookups with zero mutex locking.
   
2. **Layer 2: The Warm Path (The Global Registry)**
   If the fast path misses (e.g., this is the first row of a new query), the extension acquires a fast `sqlite3_mutex` and looks up the database's filepath in a global linked list (the Registry). If it finds the entry for this database, it increments the reference count, caches the pointer in Layer 1 using `sqlite3_set_auxdata`, and returns.

3. **Layer 3: The Cold Path (Initialization)**
   If the database filepath is not in the registry, the extension dynamically allocates a new state registry entry, initializes the read/write locks, runs the user's custom `init_fn`, inserts it into the global linked list, caches it in Layer 1, and returns.

## 3. Automated Garbage Collection
To prevent memory leaks, the extension must know when a database is closed so it can free the state memory.

It achieves this by binding a custom destructor function to the `sqlite3_set_auxdata` call. When a SQLite connection closes (or a query ends), SQLite automatically calls this destructor.
1. The destructor acquires the registry mutex.
2. It safely decrements the reference count of the state entry.
3. If the reference count hits `0`, it knows this was the absolute last connection to the database. It runs the user's `free_fn`, destroys the read/write locks, removes the entry from the linked list, and frees the memory.

## 4. The "Ghost Removal" Race Condition
A critical race condition exists during garbage collection if two connections to the same database close at the exact same millisecond. 
- Connection A hits a refcount of 0 and prepares to free the memory.
- Connection B (a new connection) sneaks in, finds the registry entry in the linked list, and increments the refcount to 1.
- Connection A finishes freeing the memory, leaving Connection B with a dangling pointer (Use-After-Free).

**The Fix (Double-Checked Locking):**
The architecture uses strict double-checked locking using a secondary mutex (`entry->ref_mutex`). During destruction, the extension holds *both* the global registry lock and the entry's ref lock. It verifies the refcount is still exactly `0` before committing to the memory deallocation, perfectly preventing the ghost removal race condition.

## 5. In-Memory Isolation
In-memory databases (`:memory:`) all share the exact same filepath string (`:memory:`). If the registry relied strictly on filepaths, all in-memory databases would accidentally share the same state.
To prevent this, the architecture intercepts `:memory:` filepaths and dynamically concatenates the raw memory address of the SQLite connection pointer (e.g., `:memory:0x1234abcd`). This guarantees perfect isolation for in-memory databases.
