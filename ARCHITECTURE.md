# Core Architecture & Design Philosophy

`sqlite-ext-core` is an ecosystem of C++11 header-only wrappers designed to eliminate the boilerplate of the SQLite C-API while maintaining exactly zero runtime overhead. 

The entire framework is architected around a set of extremely strict constraints designed to make the library embeddable in any environment, especially those lacking a standard library or exception support.

## 1. Zero-Cost Abstractions (`-nostdlib++`)

This library is designed to compile cleanly with `-fno-exceptions`, `-fno-rtti`, and `-nostdlib++`. 
- **No standard library containers**: We do not use `std::string`, `std::vector`, or `std::unique_ptr`. 
- **Zero dynamic allocations**: The core wrappers NEVER call `new` or `delete`. 
- **Header-only**: All classes and methods are marked `inline`, allowing the compiler's `-O2` optimization pass to completely erase the abstraction layers. A C++ `SqliteStatement` compiles down to the exact same machine-code assembly as manually calling `sqlite3_step()` on a raw `sqlite3_stmt*`.

## 2. The `Owned` vs `View` Pattern

Because we cannot rely on `std::string_view` or `std::unique_ptr` in a `-nostdlib++` environment, we reinvented a zero-cost ownership model applied uniformly across the entire library (Values, Strings, and Databases).

- **`View` Classes**: (e.g., `SqliteValueView`, `SqliteDatabaseView`)
  - Hold a raw pointer. 
  - Never allocate memory, and never free it.
  - Used when SQLite hands you data (e.g., in a UDF callback) and you just want C++ convenience methods.

- **`Owned` Classes**: (e.g., `SqliteValueOwned`, `SqliteDatabaseOwned`)
  - Inherit publicly from their respective `View` base class.
  - Their constructors take ownership or copy data (e.g., `sqlite3_open_v2` or `sqlite3_value_dup`).
  - Their destructors safely clean up the resource (e.g., `sqlite3_close_v2` or `sqlite3_value_free`).
  - By inheriting from `View`, they support **object slicing**. You can pass an `Owned` object by value into any function expecting a `View`, which compiles down to a raw 8-byte pointer copy with zero overhead!

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
- [**Value System (`SqliteValue`)**](docs/VALUE_ARCHITECTURE.md): The core zero-cost `Owned`/`View` wrappers over `sqlite3_value` powering heterogeneous lookups.
- [**Dynamic Buffers (`SqliteBuffer`)**](docs/BUFFER_ARCHITECTURE.md): `-nostdlib++` replacements for `std::string` and `std::vector` using `sqlite3_realloc64` that natively hook into the Value System's FNV-1a hashing engine.
- [**Blob Streams (`SqliteBlobStream`)**](docs/BLOB_STREAM_ARCHITECTURE.md): Zero-copy stream interfaces for handling large SQLite blobs without loading them entirely into memory.
- [**Online Backup (`SqliteBackup`)**](docs/BACKUP_ARCHITECTURE.md): RAII wrappers for the SQLite Online Backup API to ensure safe resource disposal during long-running background tasks.
- [**Virtual Tables (`SqliteVTable`)**](docs/VTAB_ARCHITECTURE.md): An object-oriented routing framework that maps SQLite's raw C module function pointers to safe polymorphic C++ method invocations.
- [**Extension State**](docs/EXT_STATE_ARCHITECTURE.md): Thread-safe management of global state across multiple SQLite connections.
- [**Smart Pointers**](docs/SMART_PTR_ARCHITECTURE.md): Exception-safe `SqliteUniquePtr` and `SqliteSharedPtr` implementations without `<memory>`.
- [**Custom Allocators**](docs/ALLOCATOR_ARCHITECTURE.md): Hooking into SQLite's memory arena via `sqlite3_malloc64` and `sqlite3_free`.

### Concurrency & Locks
- [**Atomics**](docs/ATOMIC_ARCHITECTURE.md): Lock-free reference counting across GCC, MSVC, and Clang intrinsics.
- [**Mutex Locks**](docs/MUTEX_LOCK_ARCHITECTURE.md): RAII wrappers over SQLite's extremely fast recursive `sqlite3_mutex`.
- [**Read-Write Locks**](docs/READWRITE_LOCK_ARCHITECTURE.md): Thread-safe concurrent readers and exclusive writers.
- [**Tiny Lock**](docs/TINY_LOCK_ARCHITECTURE.md): A blazing-fast spinlock fallback for systems lacking native RW-locks.

### Database Interaction
- [**Database Lifecycle**](docs/DB_ARCHITECTURE.md): The `Owned`/`View` model for robust connection management.
- [**Transactions & Savepoints**](docs/TRANSACTION_ARCHITECTURE.md): Exception-safe RAII rollbacks and hierarchical nested transactions.
- [**Statements**](docs/STATEMENT_ARCHITECTURE.md): Zero-cost query builders, iterators, and prepared statement caching.

### Extensibility (UDFs, Virtual Tables & Extensions)
- [**Extension Creator (`sqlite3_ext_creator.hpp`)**](docs/EXTENSION_ARCHITECTURE.md): Zero-boilerplate entrypoint definition, dynamic symbol export, and routine dispatch initialization.
- [**Scalar UDFs**](docs/UDF_ARCHITECTURE.md): Compile-time C-callback generation for User-Defined Functions.
- [**Aggregate UDFs**](docs/AGGREGATE_ARCHITECTURE.md): Deterministic memory layouts for `xStep` and `xFinal` aggregations.
- [**Table-Valued Functions (TVF)**](docs/TVF_ARCHITECTURE.md): Statically generated `sqlite3_module` structs mapping to strictly-typed C++ classes.
- [**Virtual Tables (VTAB)**](docs/VTAB_ARCHITECTURE.md): Polymorphic standard-layout routing, transactions, savepoints, and direct context state injection.
- [**Unified Extensibility (`SqliteExt`)**](include/sqlite3_ext.hpp): Symmetrical registration facade combining UDFs, Aggregates, TVFs, and Virtual Tables.
- [**Extension Examples & Tutorial**](examples/README.md): Turnkey example showcasing compilation, testing, and multi-language loading of SQLite extensions.
