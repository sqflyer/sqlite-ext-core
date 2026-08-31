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
  - `SqliteRowView` provides a 24-byte multi-source union over statements, UDF argv, and view arrays, adhering to standard `std::array` accessors and bidirectional iteration (`sqlite_reverse_iterator`).
  - Used when SQLite hands you data (e.g., in a UDF callback) and you just want C++ convenience methods.

- **`Owned` Classes**: (e.g., `SqliteValueOwned`, `SqliteDatabaseOwned`, `SqliteStringOwned`, `SqliteBlobOwned`)
  - Inherit publicly from their respective `View` base class.
  - Their constructors take ownership or copy data (e.g., `sqlite3_open_v2` or `sqlite3_value_dup`).
  - Move constructors and move assignment operators cleanly transfer ownership via `sqlite_move(other)`.
  - Their destructors safely clean up the resource (e.g., `sqlite3_close_v2` or `sqlite3_value_free`).
  - By inheriting from `View`, they support **object slicing**. You can pass an `Owned` object by value into any function expecting a `View`, which compiles down to a raw 8-byte pointer copy with zero overhead!

- **Value Container Classes**: (e.g., `SqliteValueTuple<N>`, `SqliteValueVec<N>`, `sqlite3_value_containers.hpp`)
  - `SqliteValueTuple<N = 0>`: Exact $N \times 16\text{B}$ in-situ stack array for compile-time fixed-arity primary and composite keys ($N \in [1..8]$), conforming to standard `std::array` (`front()`, `back()`, `at()`, `fill()`, `swap()`, `rbegin()`, `rend()`). $N = 0$ (default `SqliteValueTuple<>`) provides an immutable-width direct heap tuple where size is passed as constructor argument.
  - `SqliteValueVec<N = 0>`: Adaptive Small Buffer Optimized (SBO) dynamic vector living on stack for sizes $\le N$ ($N \in [1..8]$) and seamlessly spilling to heap when expanded $> N$, or direct heap vector for $N = 0$ (default `SqliteValueVec<>`). Conforms to standard `std::vector` (`insert()`, `erase()`, `assign()`, `resize(count, val)`, `shrink_to_fit()`, `capacity()`, `swap()`, `rbegin()`, `rend()`).
  - `SqliteRowOwnedWrapper`: 16-byte zero-allocation span wrapper (`SqliteValueOwned*` + `int len`) enabling uniform view and standard array operations across all containers.
  - **In-Situ Primitive Assignment (`operator=`)**: `SqliteValueOwned` directly overloads assignment for all C++ primitives (`int`, `sqlite3_int64`, `long`, `unsigned int`, `unsigned long`, `unsigned long long`, `double`, `float`, `bool`, `const char*`, `SqliteStringView`, `SqliteBlobView`), enabling direct syntax such as `row[0] = static_cast<sqlite3_int64>(i);` or `row[1] = "alpha";` with zero intermediate heap allocations.

## 3. Strict RAII (Resource Acquisition Is Initialization)

Manual memory, resource, and lock management is the leading cause of bugs in SQLite extensions. We enforce strict RAII to guarantee safety:
- **`SqliteStatement`**: Automatically calls `sqlite3_finalize` upon destruction.
- **`SqliteTransaction`**: Automatically issues a `ROLLBACK;` upon destruction unless explicitly committed.
- **`SqliteSavepoint`**: Automatically issues a `ROLLBACK TO;` upon destruction, enabling safe nested C++ transactions.
- **`SqliteBackup`**: Guarantees the source database read-lock is lifted via `sqlite3_backup_finish` in all scope-exit scenarios.
- **`SqliteExtState` Lock Guards**: Scope-bound `ReadGuard` and `WriteGuard` mutex lifecycles.

## 4. Template Metaprogramming & Compile-Time Matrix Dispatch

Writing SQLite Virtual Tables (VTAB) or Table-Valued Functions (TVF) requires building complex C-structs (`sqlite3_module`) filled with function pointers. 

Instead of forcing developers to write C-style `xConnect` / `xBestIndex` callbacks or repetitive nested switch statements, we use **Template Metaprogramming and Generic Dispatchers**:
- **Zero VTable Overhead**: We deliberately avoid virtual functions (`virtual void init() = 0`) to prevent vtable lookup overhead and dependency on a global `operator delete`.
- **Scope-Guarded Stack Dispatcher (`withSqliteRowOwned`)**: Dynamically dispatches runtime row sizes $0 \le N \le 8$ to inline stack tuples `SqliteValueTuple<N>` (16–128 bytes) and seamlessly falls back to `SqliteValueTuple<>` (default $N = 0$) direct dynamic heap allocation for sizes $> 8$, invoking user lambdas with a uniform `SqliteRowOwnedWrapper`.
- **Generic 8x8 Compile-Time Matrix Dispatch**: `SQLITE_DISPATCH_1D_8`, `SQLITE_DISPATCH_2D_8X8`, `SQLITE_WITH_ROW_OWNED_1D`, and `SQLITE_WITH_KEY_VAL_OWNED_8X8` expand runtime column/key configurations into compile-time `constexpr` specializations for any storage template or stack span in 1 line.
- **Auto-Routing Arguments**: Hidden columns in virtual tables are safely packaged into bounds-checked objects like `SqliteUdfArgs` (`SqliteRowView`) and injected seamlessly into your C++ methods.

## 5. Subsystem Architecture Guides

For a deeper dive into the specific mechanics and C++ paradigms used in individual components, refer to their dedicated architecture guides:

### Memory & State Management
- [**Value System (`SqliteValue`)**](docs/VALUE_ARCHITECTURE.md): The core zero-cost `Owned`/`View` wrappers over `sqlite3_value`, 16-byte Small Buffer Optimization, heterogeneous lookups, and SQLite subtype representations.
- [**Value Containers & Matrix Dispatch (`sqlite3_value_containers.hpp`)**](docs/VALUE_CONTAINERS_ARCHITECTURE.md): Zero-dependency value containers (`SqliteValueTuple<N>`, `SqliteValueVec<N>`), scope-guarded stack allocator (`withSqliteRowOwned`), and generic compile-time 2D matrix dispatchers.
- [**Row System (`SqliteRow`)**](docs/ROW_ARCHITECTURE.md): Universal `SqliteRowView` (24B) multiplexing prepared statement step rows, UDF argument vectors, and in-memory view arrays with zero heap allocation.
- [**Dynamic Buffers (`SqliteBuffer`)**](docs/BUFFER_ARCHITECTURE.md): `-nostdlib++` replacements for `std::string` and `std::vector` using `sqlite3_realloc64` that natively hook into the Value System's FNV-1a hashing engine.
- [**Blob Streams (`SqliteBlobStream`)**](docs/BLOB_STREAM_ARCHITECTURE.md): Zero-copy stream interfaces for handling large SQLite blobs without loading them entirely into memory.
- [**Online Backup (`SqliteBackup`)**](docs/BACKUP_ARCHITECTURE.md): RAII wrappers for the SQLite Online Backup API to ensure safe resource disposal during long-running background tasks.
- [**Virtual Tables (`SqliteVTable`)**](docs/VTAB_ARCHITECTURE.md): An object-oriented routing framework that maps SQLite's raw C module function pointers to safe polymorphic C++ method invocations.
- [**Extension State**](docs/EXT_STATE_ARCHITECTURE.md): Thread-safe management of global state across multiple SQLite connections.
- [**Smart Pointers**](docs/SMART_PTR_ARCHITECTURE.md): Exception-safe `SqliteUniquePtr` and `SqliteSharedPtr` implementations without `<memory>`.
- [**Custom Allocators**](docs/ALLOCATOR_ARCHITECTURE.md): Hooking into SQLite's memory arena via `sqlite3_malloc64`

### Synchronization & Timing
- [**Extension Coroutine Pool (`SqliteExtCoroPool`)**](docs/CORO_EXT_POOL_ARCHITECTURE.md): Zero-collision, reference-counted extension-presence worker pool registry for process-wide shared execution across multiple SQLite database connections with OS virtual address keying and automatic `xDestroy` teardown.
- [**M:N Coroutine Scheduler & Worker Pool (`SqliteCoroScheduler`)**](docs/CORO_SCHED_ARCHITECTURE.md): Freestanding M:N cooperative task scheduler multiplexing $M$ coroutine tasks across $N$ OS worker threads (or main-thread event loops for WASM/TVFs), complete with AB-BA deadlock elimination, synchronized Win32 fiber lifecycle mutex protection, and process-wide atomic reference-counted singleton management.
- [**Coroutine, Generator & Fiber Subsystem (`SqliteCoroutine`)**](docs/COROUTINE_ARCHITECTURE.md): Freestanding stackful fibers, recursive deep-stack generators (`SqliteFiberGenerator<T>`), and freestanding C++20 `co_yield` lowering via native Win32 Fibers and POSIX `ucontext_t` without `<coroutine>`.
- [**Threading & Async Subsystem (`SqliteThread`)**](docs/THREAD_ARCHITECTURE.md): Freestanding C99 and C++11 threading primitives, condition variables (`SqliteConditionVariable`), and non-virtual lambda closure trampolines mapping to native Win32/POSIX kernel APIs without `<thread>`.
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
- [**Coroutine Table-Valued Functions (TVF)**](docs/TVF_CORO_ARCHITECTURE.md): Zero-boilerplate single generator functions using Stackful Fibers or Stackless C++20 `co_yield` with automatic column multiplexing.
- [**Virtual Tables (VTAB)**](docs/VTAB_ARCHITECTURE.md): Polymorphic standard-layout routing, transactions, savepoints, and direct context state injection.
- [**Unified Extensibility (`SqliteExt` / `sqlite3_ext.h`)**](include/sqlite3_ext.hpp): Symmetrical registration facade combining UDFs, Aggregates, TVFs, and Virtual Tables.
- [**C++ Extension Tutorial**](example-cpp/README.md): Turnkey C++ example showcasing compilation, testing, and multi-language loading.
- [**Pure C Extension Tutorial**](example-c/README.md): Turnkey Pure C (C99/C11) example demonstrating state management and UDF registration.

### Key Indexing, Macro Synthesis & Reference Matrix
- [**Small Buffer Optimization (SBO) Architecture (`SBO_OPTIMIZATIONS.md`)**](SBO_OPTIMIZATIONS.md): 16-byte scalar SBO, 100% stack data density, $O(1)$ active tag scanning (`0x20`), 64-byte L1 cache line calculations, and 2-register spans.
- [**Value Containers & Matrix Dispatch (`sqlite3_value_containers.hpp`)**](docs/VALUE_CONTAINERS_ARCHITECTURE.md): Zero-dependency value containers (`SqliteValueTuple<N>`, `SqliteValueVec<N>`), scope-guarded stack allocator (`withSqliteRowOwned`), and generic compile-time 2D matrix dispatchers.
- [**Unified Macro Architecture (`docs/MACROS.md`)**](docs/MACROS.md): 5-tier macro synthesizer suite for standard container alignment, array accessors & iterators, vector/tuple modifiers, composite hashing, scalar & container relational operators, and C++20 transparent functors.
- [**C++ Type & Container Comparison Matrix (`docs/COMPARISON_MATRIX.md`)**](docs/COMPARISON_MATRIX.md): Comprehensive comparative reference across all value types, containers, row views, macros, and transparent STL functors.

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

## 8. Unified Macro Synthesizers & Transparent Functors

To enforce complete standard library independence while providing modern C++ ergonomics, `sqlite-ext-core` employs a unified macro synthesizer architecture:
- **Single-Burst SIMD Initialization (`SqliteValueOwned::static_null_array()`)**: Container constructors leverage a pre-populated static 128-byte array of 8 canonical `SQLITE_NULL` instances (`tag.raw = 0xA0`), lowered by Clang/GCC/MSVC directly into 1–4 vector register operations (`vmovups`) executing in 1–2 CPU clock cycles (~0.3–0.6 ns).
- **Array Accessors & Hashing (`SQLITE_DERIVE_ARRAY_ACCESSORS`, `SQLITE_DERIVE_ARRAY_HASH`)**: Inlined typed extractions and 64-bit MurmurHash2 composite hashing with $O(1)$ scalar fast-paths and sequential mixer folding.
- **Range-Based Iteration (`SQLITE_DERIVE_ARRAY_ITERATOR`)**: Standard C++11 forward `Iterator`, `begin()`, and `end()` for range-based for loops (`for (auto val : container)`) across all array, row, and key types.
- **Relational Operators (`SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS`, `SQLITE_DERIVE_ALL_SCALAR_RELATIONAL_OPS`)**: Full operator suites (`==`, `!=`, `<`, `<=`, `>`, `>=`) supporting multi-column lexicographical ordering and scalar comparisons honoring SQLite's type collation (`NULL < NUMERIC < TEXT < BLOB`).
- **C++20 Transparent Functors (`SQLITE_DERIVE_TRANSPARENT_EQUAL`, `SQLITE_DERIVE_TRANSPARENT_LESS`)**: Synthesizes `using is_transparent = void;` functors enabling zero-allocation lookups in `std::unordered_map` (Swiss Tables) and `std::map` (B-Trees).

## 9. Modular Test Suite Organization

The test framework is strictly modularized by domain and isolation level:
- **`tests/cpp_value/`**: Scalar and polymorphic value types (`SqliteValueOwned`, `SqliteValueView`, `SqliteStringView`, `SqliteBlobView`), SBO heap transitions, subtype tagging, and scalar operator overloads.
- **`tests/cpp_row/`**: Universal row wrappers (`SqliteRowView`, `SqliteRowOwnedWrapper`), multi-column row relational comparisons, and scope-guarded stack execution (`withSqliteRowOwned`).
- **`tests/cpp_value_containers/`**: Dedicated container verification split into core mechanics (`test_value_containers.cpp`), cross-container relational matrix (`test_value_containers_comparisons.cpp`), and C++14 STL container integration (`test_value_containers_std.cpp`).
- **`tests/threads/`**: Threading primitives, condition variables, stackful fibers (`SqliteCoroutine`), streaming generators (`SqliteFiberGenerator`), M:N schedulers (`SqliteCoroScheduler`), and multi-database extension pools (`SqliteExtCoroPool`).


