# C/C++ SQLite Extension Core (`sqlite-ext-core`)

A collection of foundational, zero-dependency C and C++ headers providing thread-safe state management, garbage collection, and native abstractions for building advanced SQLite extensions.

This repository serves as the native C and C++ counterpart to the Rust `sqlite-ext-core` library.

## Currently Implemented

### 1. Zero-Dependency C++ Memory Allocator (`sqlite3_allocator.hpp`)
A fully freestanding C++ allocator that brings `std::allocator` and `std::construct_at` semantics to SQLite extensions compiled with `-nostdlib++` and `-fno-exceptions`.

#### Key Features:
- **Zero-Dependency Construction**: Leverages proprietary `operator new` tag trickery to safely invoke C++ constructors natively without the `<new>` header.
- **SQLite Profiler Integration**: Provides `sqlite_new` and `sqlite_delete` to flawlessly route all C++ instantiations through `sqlite3_malloc` and `sqlite3_free`, keeping memory limits perfectly tracked by the core engine.
- **Decoupled Array Architecture**: Explicitly separates raw memory allocation (`sqlite_new_array`) from construction (`sqlite_construct_at`) to completely eliminate the hidden length overhead of standard C++ `new[]`.
- **Perfect Forwarding**: Implements `sqlite_move_ptr` and `sqlite_forward` to enable highly optimized, variadic constructor forwarding without `#include <utility>`.
- **Smart Pointer Ready**: Acts as the foundational memory and lifecycle layer for components like `SqliteSharedPtr` and `SqliteUniquePtr`.

#### Documentation
- [Allocator Quickstart](docs/ALLOCATOR_README.md)
- [Allocator Architecture](docs/ALLOCATOR_ARCHITECTURE.md)

### 2. Synchronization Primitives (`sqlite3_atomic.h`, `sqlite3_tiny_lock`, `sqlite3_mutex_lock`, `sqlite3_rw_lock`)
A zero-dependency, freestanding suite of cross-platform atomics and locks designed for high-concurrency extensions, WebAssembly ports, and OS kernels.

#### Key Features:
- **`sqlite3_atomic.h`**: Explicitly sized (8, 16, 32, 64-bit) atomics wrapping GCC/Clang built-ins and MSVC intrinsics to guarantee perfectly typed cross-platform memory operations without `<stdatomic.h>`.
- **`sqlite3_tiny_lock`**: A microscopic (4-byte) hybrid spinlock. On native hardware, it acts as a blistering-fast CPU-yielding spinlock (`PAUSE`/`YIELD`). On WebAssembly, it dynamically transforms into a true 0% CPU sleeping mutex via `memory.atomic.wait32`.
- **`sqlite3_mutex_lock`**: An owning C++ wrapper over SQLite's native `sqlite3_mutex_alloc`. Mimics `std::mutex` and `std::lock_guard` perfectly, while safely handling `nullptr` mutexes in single-threaded SQLite compilations.
- **`sqlite3_rw_lock`**: A cross-platform Read/Write lock that seamlessly maps to Windows `SRWLOCK`, POSIX `pthread_rwlock_t`, and WASM `memory.atomic.wait32` (via `TinyLock`). Includes zero-overhead C++ RAII wrappers (`SqliteReadGuard` / `SqliteWriteGuard`) to maximize read concurrency while guaranteeing exception-safe locking.

#### Documentation
- [Atomic Architecture](docs/ATOMIC_ARCHITECTURE.md)
- [TinyLock Architecture](docs/TINY_LOCK_ARCHITECTURE.md)
- [Mutex Lock Architecture](docs/MUTEX_LOCK_ARCHITECTURE.md)
- [Read/Write Lock Quickstart](docs/READWRITE_LOCK_README.md)
- [Read/Write Lock Architecture](docs/READWRITE_LOCK_ARCHITECTURE.md)

### 3. Per-Database Shared State Manager (`sqlite3_ext_state.h` / `.hpp`)
Maintaining state (like connection pools, LRU caches, or simple counters) inside a SQLite extension is notoriously difficult due to SQLite's architecture, where extensions are loaded once per process but are used concurrently across multiple database connections.

The state manager solves this by automatically generating a thread-safe, garbage-collected, **Per-Database Shared State Registry**.

#### Key Features:
- **Zero-Boilerplate APIs**: Available as a C macro (`SQLITE_EXTENSION_STATE`) for pure C extensions, and a C++ template (`SqliteExtState<T>`) for C++ extensions.
- **3-Layer Caching Architecture**: Implements O(1) nanosecond-fast state retrieval using SQLite's `sqlite3_set_auxdata` cache, falling back to a global registry.
- **Automated Garbage Collection**: Integrates directly with SQLite's `xDestroy` connection hooks to automatically free memory when the last connection to a database closes.
- **Embedded C++ Objects**: The C++ template seamlessly manages memory lifecycles via placement `new` and pseudo-destructors to support nested C++ objects (like `std::string`).
- **Ghost-Removal Protection**: Features bullet-proof Double-Checked Locking to prevent Use-After-Free race conditions during concurrent connection teardowns.
- **Cross-Platform Thread Safety**: Native Read/Write locks for Windows (`SRWLOCK`), macOS/Linux (`pthread_rwlock_t`), and WebAssembly (`memory.atomic.wait32` via `TinyLock`).
- **C++ RAII Lock Guards**: The C++ template natively provides `ReadGuard` and `WriteGuard` classes to guarantee exception-safe, scope-based locking.

#### Documentation
- [State Manager Quickstart](docs/EXT_STATE_README.md)
- [State Manager Internal Architecture](docs/EXT_STATE_ARCHITECTURE.md)

### 4. C++ RAII Data Types (`sqlite3_value_keys.hpp`)
Zero-dependency C++ RAII wrappers for SQLite core data types designed for zero-allocation lookups and heterogeneous map keys.

#### Key Features:
- **SQLite Integration APIs**: Provides zero-overhead `bind()` and `result()` methods directly on wrappers to easily interoperate with `sqlite3_stmt` parameters and `sqlite3_context` returns.
- **Zero-Allocation Lookups**: Provides non-owning `View` wrappers (`SqliteStringView`, `SqliteBlobView`, `SqliteValueView`) to prevent expensive memory allocations during C++ map key lookups.
- **Heterogeneous Lookups**: Natively supports comparing `View`s against heavy, memory-managed `Owned` classes using SIMD-accelerated C `memcmp` comparisons.
- **Accurate Collation**: Fully conforms to official SQLite collation sorting rules (`NULL < NUMERIC < TEXT < BLOB`).
- **Polymorphic Variants**: Safely store Integer, Float, Text, and Blob payloads inside the exact same `std::map` using the polymorphic `SqliteValueOwned` wrapper.
- **Ergonomic String Builders**: Easily construct dynamic strings without a database handle using standard `(const char*)` constructors, or safely instantiate them inside User-Defined Functions with `(sqlite3_context*)` wrappers.
- **Zero STL Overhead**: Fully implemented using raw C-pointers and SQLite's native memory profilers (`sqlite3_malloc`). No `<string>` or `<vector>` overhead. Perfect for constrained environments like WASM.

#### Documentation
- [Value Keys README](docs/VALUE_KEYS_README.md)
- [Value Keys Internal Architecture](docs/VALUE_KEYS_ARCHITECTURE.md)

## Building and Testing
This repository includes a robust test suite to verify the thread-safety and memory-safety of the extensions under immense load.

To run the integration tests across both the C and C++ extensions:
```bash
make test
```
