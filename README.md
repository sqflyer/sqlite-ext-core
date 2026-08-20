# C/C++ SQLite Extension Core (`sqlite-ext-core`)

A collection of foundational, zero-dependency C and C++ headers providing thread-safe state management, garbage collection, and native abstractions for building advanced SQLite extensions.

This repository serves as the native C and C++ counterpart to the Rust `sqlite-ext-core` library.

## Currently Implemented

### 1. Per-Database Shared State Manager (`sqlite3_ext_state.h`)
Maintaining state (like connection pools, LRU caches, or simple counters) inside a SQLite extension is notoriously difficult due to SQLite's architecture, where extensions are loaded once per process but are used concurrently across multiple database connections.

`sqlite3_ext_state.h` provides a single macro (`SQLITE_EXTENSION_STATE`) that solves this by automatically generating a thread-safe, garbage-collected, **Per-Database Shared State Registry**.

#### Key Features:
- **3-Layer Caching Architecture**: Implements O(1) nanosecond-fast state retrieval using SQLite's `sqlite3_set_auxdata` cache, falling back to a global registry.
- **Automated Garbage Collection**: Integrates directly with SQLite's `xDestroy` connection hooks to automatically free memory when the last connection to a database closes.
- **Ghost-Removal Protection**: Features bullet-proof Double-Checked Locking to prevent Use-After-Free race conditions during concurrent connection teardowns.
- **Cross-Platform Thread Safety**: Native Read/Write locks for Windows (`SRWLOCK`), macOS/Linux (`pthread_rwlock_t`), and WebAssembly (`sqlite3_mutex`).
- **C++ RAII Lock Guards**: Auto-generates `ReadGuard` and `WriteGuard` classes via `#ifdef __cplusplus` to guarantee exception-safe, scope-based locking.

#### Documentation
- [State Manager Quickstart](docs/EXT_STATE_README.md)
- [State Manager Internal Architecture](docs/EXT_STATE_ARCHITECTURE.md)
- Usage examples can be found in `tests/integration/c_extension/myext.c` and `tests/integration/cpp_extension/myext.cpp`.

## Building and Testing
This repository includes a robust Go-based concurrency and lazy-loading test suite to verify the thread-safety and memory-safety of the extensions under immense load.

To run the integration tests across both the C and C++ extensions:
```bash
make test
```
