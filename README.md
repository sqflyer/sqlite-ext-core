# C/C++ SQLite Extension Core (`sqlite-ext-core`)

A collection of foundational, zero-dependency C and C++ headers providing thread-safe state management, garbage collection, and native abstractions for building advanced SQLite extensions.

This repository serves as the native C and C++ counterpart to the Rust `sqlite-ext-core` library.

## Currently Implemented

### 1. Per-Database Shared State Manager (`sqlite3_ext_state.h` / `.hpp`)
Maintaining state (like connection pools, LRU caches, or simple counters) inside a SQLite extension is notoriously difficult due to SQLite's architecture, where extensions are loaded once per process but are used concurrently across multiple database connections.

The state manager solves this by automatically generating a thread-safe, garbage-collected, **Per-Database Shared State Registry**.

#### Key Features:
- **Zero-Boilerplate APIs**: Available as a C macro (`SQLITE_EXTENSION_STATE`) for pure C extensions, and a C++ template (`SqliteExtState<T>`) for C++ extensions.
- **3-Layer Caching Architecture**: Implements O(1) nanosecond-fast state retrieval using SQLite's `sqlite3_set_auxdata` cache, falling back to a global registry.
- **Automated Garbage Collection**: Integrates directly with SQLite's `xDestroy` connection hooks to automatically free memory when the last connection to a database closes.
- **Embedded C++ Objects**: The C++ template seamlessly manages memory lifecycles via placement `new` and pseudo-destructors to support nested C++ objects (like `std::string`).
- **Ghost-Removal Protection**: Features bullet-proof Double-Checked Locking to prevent Use-After-Free race conditions during concurrent connection teardowns.
- **Cross-Platform Thread Safety**: Native Read/Write locks for Windows (`SRWLOCK`), macOS/Linux (`pthread_rwlock_t`), and WebAssembly (`sqlite3_mutex`).
- **C++ RAII Lock Guards**: The C++ template natively provides `ReadGuard` and `WriteGuard` classes to guarantee exception-safe, scope-based locking.

#### Documentation
- [State Manager Quickstart](docs/EXT_STATE_README.md)
- [State Manager Internal Architecture](docs/EXT_STATE_ARCHITECTURE.md)

### 2. SQLite Shared Pointers (`sqlite3_shared_ptr.h` / `.hpp`)
Zero-dependency C macro and C++ templates (`sqlite3_shared_ptr<T>`) for thread-safe, reference-counted memory allocation that integrates directly into SQLite's memory manager (`sqlite3_malloc`). Allows sharing dynamic payloads safely across UDF boundaries.

### 3. C++ RAII Data Types (`sqlite3_value_keys.hpp`)
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
This repository includes a robust Go-based concurrency and lazy-loading test suite to verify the thread-safety and memory-safety of the extensions under immense load.

To run the integration tests across both the C and C++ extensions:
```bash
make test
```
