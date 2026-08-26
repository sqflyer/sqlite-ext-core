# Core Architecture & Design Philosophy

`sqlite-ext-core` is an ecosystem of C++11 header-only wrappers designed to eliminate the boilerplate of the SQLite C-API while maintaining exactly zero runtime overhead. 

The entire framework is architected around a set of extremely strict constraints designed to make the library embeddable in any environment, especially those lacking a standard library or exception support.

## 1. Zero-Cost Abstractions (`-nostdlib++` & `/NODEFAULTLIB`)

This library is architected to compile cleanly with complete elimination of C++ standard runtime dependencies across all major toolchains:
- **GCC / Clang**: `-nostdlib++ -fno-exceptions -fno-rtti`
- **MSVC (`cl.exe`)**: `/GR- /EHs-c- /link /NODEFAULTLIB:msvcprt.lib /NODEFAULTLIB:libcpmt.lib`

Core Guarantees:
- **No standard library containers or headers**: We do not use `std::string`, `std::vector`, `std::unique_ptr`, `<utility>`, or `<new>`.
- **Zero-Dependency Move Semantics & Forwarding**: Provides `sqlite_move` (and `sqlite_move_ptr`) and `sqlite_forward` in `sqlite3_allocator.hpp` to enable `std::move` and `std::forward` capabilities without `<utility>`.
- **Zero dynamic allocations**: The core wrappers NEVER call standard `new` or `delete`, routing any needed allocations through `sqlite3_malloc` via `sqlite_new` and `sqlite_construct_at`.
- **Header-only**: All classes and methods are marked `inline`, allowing the compiler's `-O2` optimization pass to completely erase the abstraction layers. A C++ `SqliteStatement` compiles down to the exact same machine-code assembly as manually calling `sqlite3_step()` on a raw `sqlite3_stmt*`.

## 2. The `Owned` vs `View` Pattern

Because we cannot rely on `std::string_view` or `std::unique_ptr` in a `-nostdlib++` environment, we reinvented a zero-cost ownership model applied uniformly across the entire library (Values, Strings, Buffers, and Databases).

- **`View` Classes**: (e.g., `SqliteValueView`, `SqliteDatabaseView`, `SqliteStringView`, `SqliteBlobView`, `SqliteRowView`)
  - Hold a raw pointer or a tagged union of pointers.
  - Never allocate memory, and never free it.
  - Used when SQLite hands you data (e.g., in a UDF callback) and you just want C++ convenience methods.

- **`Owned` Classes**: (e.g., `SqliteValueOwned`, `SqliteDatabaseOwned`, `SqliteStringOwned`, `SqliteBlobOwned`)
  - Inherit publicly from their respective `View` base class.
  - Their constructors take ownership or copy data (e.g., `sqlite3_open_v2` or `sqlite3_value_dup`).
  - Move constructors and move assignment operators cleanly transfer ownership via `sqlite_move(other)`.
  - Their destructors safely clean up the resource (e.g., `sqlite3_close_v2` or `sqlite3_value_free`).
  - By inheriting from `View`, they support **object slicing**. You can pass an `Owned` object by value into any function expecting a `View`, which compiles down to a raw 8-byte pointer copy with zero overhead!

- **`OwnedArray` Classes**: (e.g., `SqliteValueOwnedStaticArray<N>`, `SqliteValueOwnedDynamicArray`, `SqliteValueOwnedArray<N>`)
  - Manage contiguous RAII arrays of `SqliteValueOwned` elements.
  - Static variant lives entirely on the stack (0 mallocs); dynamic variant uses `sqlite3_realloc64` for in-place growth.
  - Act as the **foundational base classes** for `SqliteRowStatic<N>` and `SqliteRowDynamic` in `sqlite3_row.hpp`, which inherit from them and add row-domain APIs (`.view()`, `.column_count()`, `operator SqliteRowView()`).

## 3. Strict RAII (Resource Acquisition Is Initialization)

Manual memory, resource, and lock management is the leading cause of bugs in SQLite extensions. We enforce strict RAII to guarantee safety:
- **`SqliteStatement`**: Automatically calls `sqlite3_finalize` upon destruction.
- **`SqliteTransaction`**: Automatically issues a `ROLLBACK;` upon destruction unless explicitly committed.
- **`SqliteSavepoint`**: Automatically issues a `ROLLBACK TO;` upon destruction, enabling safe nested C++ transactions.
- **`SqliteBackup`**: Guarantees the source database read-lock is lifted via `sqlite3_backup_finish` in all scope-exit scenarios.
- **`SqliteExtState` Lock Guards**: Scope-bound `ReadGuard` and `WriteGuard` mutex lifecycles.

## 4. Template Metaprogramming

Writing SQLite Virtual Tables (VTAB) or Table-Valued Functions (TVF) requires building complex C-structs (`sqlite3_module`) filled with function pointers. 

Instead of forcing developers to write C-style `xConnect` / `xBestIndex` callbacks, we use **Template Metaprogramming**. Classes like `SqliteTvfModule<T>` statically generate the C-struct at compile-time by binding the C-callbacks to your strongly-typed C++ class methods. 

This provides:
- **Zero VTable Overhead**: We deliberately avoid virtual functions (`virtual void init() = 0`) to prevent vtable lookup overhead and dependency on a global `operator delete`.
- **Auto-Routing Arguments**: Hidden columns in virtual tables are safely packaged into bounds-checked objects like `SqliteUdfArgs` and injected seamlessly into your C++ methods.

## 5. Subsystem Architecture Guides

For a deeper dive into the specific mechanics and C++ paradigms used in individual components, refer to their dedicated architecture guides:

### Memory & State Management
- [**Value System (`SqliteValue`)**](docs/VALUE_ARCHITECTURE.md): The core zero-cost `Owned`/`View` wrappers over `sqlite3_value`, heterogeneous lookups, and the `SqliteValueOwnedStaticArray<N>` / `SqliteValueOwnedDynamicArray` contiguous array classes that form the base of the Row system.
- [**Row System (`SqliteRow`)**](docs/ROW_ARCHITECTURE.md): Two-tier hierarchy — `SqliteValueOwnedStaticArray<N>` / `SqliteValueOwnedDynamicArray` (value.hpp base) → `SqliteRowStatic<N>` / `SqliteRowDynamic` (row.hpp extension). Universal `SqliteRowView` (24B) multiplexes statements, argv vectors, and owned arrays. `SqliteRowUtil::copy_from_view` provides loop-optimized shared construction.
- [**Dynamic Buffers (`SqliteBuffer`)**](docs/BUFFER_ARCHITECTURE.md): `-nostdlib++` replacements for `std::string` and `std::vector` using `sqlite3_realloc64` that natively hook into the Value System's FNV-1a hashing engine.
- [**Blob Streams (`SqliteBlobStream`)**](docs/BLOB_STREAM_ARCHITECTURE.md): Zero-copy stream interfaces for handling large SQLite blobs without loading them entirely into memory.
- [**Online Backup (`SqliteBackup`)**](docs/BACKUP_ARCHITECTURE.md): RAII wrappers for the SQLite Online Backup API to ensure safe resource disposal during long-running background tasks.
- [**Virtual Tables (`SqliteVTable`)**](docs/VTAB_ARCHITECTURE.md): An object-oriented routing framework that maps SQLite's raw C module function pointers to safe polymorphic C++ method invocations.
- [**Extension State**](docs/EXT_STATE_ARCHITECTURE.md): Thread-safe management of global state across multiple SQLite connections.
- [**Smart Pointers**](docs/SMART_PTR_ARCHITECTURE.md): Exception-safe `SqliteUniquePtr` and `SqliteSharedPtr` implementations without `<memory>`.
- [**Custom Allocators**](docs/ALLOCATOR_ARCHITECTURE.md): Hooking into SQLite's memory arena via `sqlite3_malloc64` and `sqlite3_free`.

### Synchronization & Timing
- [**Time & High-Resolution Clock (`SqliteClock`)**](docs/TIME_ARCHITECTURE.md): Monotonic timers, wall-clock epoch timestamps, and system timezone detection without `<chrono>`.
- [**Lock Base & RAII Hierarchy (`SqliteLockBase` / `SqliteGuardBase`)**](include/sqlite3_lock_base.hpp): Non-copyable, non-movable base classes providing zero-overhead, vtable-free interfaces for all locks and generic template guards (`SqliteLockGuard`, `SqliteBasicReadGuard`, `SqliteBasicWriteGuard`).
- [**Atomics**](docs/ATOMIC_ARCHITECTURE.md): Lock-free reference counting across GCC, MSVC, and Clang intrinsics.
- [**Mutex Locks**](docs/MUTEX_LOCK_ARCHITECTURE.md): Pure C `sqlite3_mutex_lock` and C++ `SqliteMutex` RAII wrappers over SQLite's native `sqlite3_mutex`.
- [**Read-Write Locks**](docs/READWRITE_LOCK_ARCHITECTURE.md): Thread-safe concurrent readers and exclusive writers with OS native optimizations (`SRWLOCK`, `pthread_rwlock_t`).
- [**Tiny Lock**](docs/TINY_LOCK_ARCHITECTURE.md): A blazing-fast 1-byte spinlock fallback on native hardware and 0% CPU futex on WebAssembly.
- [**Pluggable State Synchronization**](docs/EXT_STATE_ARCHITECTURE.md): Pluggable lock policy architecture across Pure C (`_RW`, `_TINY`, `_MUTEX`) and C++ (`SqliteExtState<T, LockPolicy>`).

### Database Interaction
- [**Database Lifecycle & Event Hooks**](docs/DB_ARCHITECTURE.md): The `Owned`/`View` model for robust connection management, diagnostics, and compile-time template trampolines for `update`, `commit`, `rollback`, `wal`, and `progress` hooks.
- [**Transactions & Savepoints**](docs/TRANSACTION_ARCHITECTURE.md): Exception-safe RAII rollbacks and hierarchical nested transactions.
- [**Statements**](docs/STATEMENT_ARCHITECTURE.md): Zero-cost query builders, iterators, and prepared statement caching.

### Extensibility (UDFs, Virtual Tables & Extensions)
- [**Extension Creators (`sqlite3_ext_creator.h` / `.hpp`)**](docs/EXTENSION_ARCHITECTURE.md): Zero-boilerplate entrypoint definition, dynamic symbol export, and routine dispatch initialization for Pure C and C++.
- [**Scalar UDFs**](docs/UDF_ARCHITECTURE.md): Compile-time C-callback generation for User-Defined Functions.
- [**Aggregate UDFs**](docs/AGGREGATE_ARCHITECTURE.md): Deterministic memory layouts for `xStep` and `xFinal` aggregations.
- [**Table-Valued Functions (TVF)**](docs/TVF_ARCHITECTURE.md): Statically generated `sqlite3_module` structs mapping to strictly-typed C++ classes.
- [**Virtual Tables (VTAB)**](docs/VTAB_ARCHITECTURE.md): Polymorphic standard-layout routing, transactions, savepoints, and direct context state injection.
- [**Unified Extensibility (`SqliteExt` / `sqlite3_ext.h`)**](include/sqlite3_ext.hpp): Symmetrical registration facade combining UDFs, Aggregates, TVFs, and Virtual Tables.
- [**C++ Extension Tutorial**](examples/README.md): Turnkey C++ example showcasing compilation, testing, and multi-language loading.
- [**Pure C Extension Tutorial**](example-c/README.md): Turnkey Pure C (C99/C11) example demonstrating state management and UDF registration.

## 6. Dual Build System & Compiler Parity

The repository maintains strict parity across two native build pipelines:
- **POSIX / MSYS2 (`Makefile`)**: Drives `gcc` and `clang` compilers with `-nostdlib++` flags.
- **Windows Native (`make.bat`)**: Drives Microsoft Visual C++ (`cl.exe`) batch scripts.

### MSVC Architectural Model
1. **Isolated C-Runtime Isolation**: Pre-compiled SQLite headers and import libraries are staged under `deps/sqlite3/` (tracked via Git LFS), avoiding any cross-pollution with MinGW CRT headers.
2. **Link-Time Standard Library Prohibition**: Passing `/link /NODEFAULTLIB:msvcprt.lib /NODEFAULTLIB:libcpmt.lib` ensures the Microsoft Linker immediately rejects any code paths attempting to introduce standard C++ runtime symbols.
3. **Deterministic DLL Discovery**: Windows `.bat` test scripts co-locate required SQLite runtime DLLs directly into local `bin/` execution folders to guarantee isolated, collision-free runtime execution across concurrent test runs.

## 7. OOM Resilience & Multi-Translation-Unit (Multi-TU) Safety

### Exception-Free OOM Hardening
Because all code compiles with `-fno-exceptions` (`/EHs-c-`), allocation failures in constructors never throw `std::bad_alloc`. 
- **Non-Throwing Valid States**: `SqliteValueOwned`, `SqliteStringOwned`, `SqliteBlobOwned`, `SqliteBuffer`, `SqliteString`, and `SqliteStatement` guarantee non-null / valid checking via `.is_valid()` and `explicit operator bool()`.
- **Safe Degradation**: Operations on empty or unallocated objects return deterministic error codes (`SQLITE_NOMEM`, `SQLITE_MISUSE`) or safe default representations without dereferencing null pointers.

### Multi-Translation-Unit & ODR Safety
Large SQLite extensions often span multiple `.cpp` files. `sqlite3_ext_state.hpp` leverages C++11 template static member guarantees so that multiple translation units in the same extension shared library automatically link to a single unified state registry without duplicate symbol collisions or disjoint static instances.
