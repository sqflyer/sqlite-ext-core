# Read/Write Lock Architecture

## The Problem
When building high-concurrency SQLite extensions (such as custom data structures, caches, or state managers), using standard exclusive mutexes for all operations creates severe bottlenecks. If multiple threads simply want to *read* a shared state without modifying it, an exclusive mutex forces them to serialize and wait for each other, destroying read throughput.

To solve this, a Read/Write Lock (Shared/Exclusive Lock) allows:
1. **Multiple readers** to acquire the lock simultaneously.
2. **Only a single writer** to acquire the lock exclusively (blocking all other readers and writers).

## The Solution: Zero-Dependency Cross-Platform RW Locks
Because SQLite extensions must compile down to a single `.c` or `.so`/`.dll`/`.wasm` file without heavy standard library dependencies, we cannot rely on `std::shared_mutex` (which introduces heavy `<mutex>` standard library bloat).

Instead, `sqlite-ext-core` provides `sqlite3_rw_lock.h`, an ultra-lightweight abstraction that maps directly to the underlying Operating System's native locking primitives at compile-time.

### OS Native Mapping

#### 1. Windows (`_WIN32`)
On Windows, the lock compiles directly to `SRWLOCK` (Slim Reader/Writer Lock).
- **Pros**: It is incredibly fast, takes up only the size of a single pointer (8 bytes), and requires no dynamic heap allocation or explicit destruction.

#### 2. POSIX / Linux / macOS
On POSIX systems, the lock maps to `pthread_rwlock_t`.
- **Pros**: The industry standard for POSIX threading. It provides high-performance read-write isolation natively at the kernel/scheduler level.

#### 3. WebAssembly (`__wasm__` / `__EMSCRIPTEN__`)
WASM environments lack standard `pthread` read/write locks out of the box. 
- **The WASM Fallback**: Instead of pulling in massive polyfills, we automatically fallback to our own `sqlite3_tiny_lock`.
- **Why TinyLock?**: Because `TinyLock` natively compiles down to the `memory.atomic.wait32` instruction. This allows the lock to completely pause execution and put the thread to sleep (0% CPU) without requiring any dynamic memory allocation, serving as a perfect lightweight substitute.
- **Caveat**: Because it degrades into a standard mutually-exclusive lock, multiple concurrent readers will block each other on WASM. Since WASM extensions generally run in a single thread (or heavily constrained Web Workers), this is an acceptable trade-off to avoid the massive payload size of shipping a full POSIX threading implementation to the browser. Note that read-heavy workloads on WASM won't parallelize exactly like native code.

## The C++ Abstraction Layer
While the core logic is written in pure C macros for ultimate portability (`sqlite3_rw_lock.h`), we provide a zero-overhead C++ wrapper (`sqlite3_rw_lock.hpp`) to bring modern RAII (Resource Acquisition Is Initialization) semantics to extension developers.

### `SqliteRwLock`
A C++ class that holds the `sqlite3_rw_lock` struct. 
- Its constructor calls `sqlite3_rw_lock_init`.
- Its destructor calls `sqlite3_rw_lock_destroy`.
- It completely hides the C macro names behind clean `lock_read()`, `unlock_read()`, `lock_write()`, and `unlock_write()` methods.

### RAII Guards
To guarantee exception safety (even when compiled with `-fno-exceptions`), we provide `SqliteReadGuard` and `SqliteWriteGuard`.
By binding lock acquisition to object construction, and lock release to object destruction, developers can safely lock state without ever worrying about accidental deadlocks caused by early `return` statements or complex control flows.
