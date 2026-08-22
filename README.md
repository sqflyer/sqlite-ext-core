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

### 2. Synchronization Primitives (`sqlite3_atomic.h` / `.hpp`, `sqlite3_tiny_lock`, `sqlite3_mutex_lock`, `sqlite3_rw_lock`)
A zero-dependency, freestanding suite of cross-platform atomics and locks designed for high-concurrency extensions, WebAssembly ports, and OS kernels.

#### Key Features:
- **`sqlite3_atomic.h` / `.hpp`**: Explicitly sized (8, 16, 32, 64-bit) C atomics wrapping GCC/Clang built-ins and MSVC intrinsics. The `.hpp` header adds a zero-dependency C++ SFINAE engine that perfectly mimics the polymorphism of `<atomic>` automatically detecting variable widths at compile-time.
- **`sqlite3_tiny_lock`**: A microscopic (1-byte) hybrid lock. On native hardware, it acts as a blistering-fast, cache-friendly TTAS (Test and Test-And-Set) spinlock to prevent MESI bouncing storms. On WebAssembly, it dynamically scales to 4-bytes and transforms into a true 0% CPU sleeping mutex via `memory.atomic.wait32`.
- **`sqlite3_mutex_lock`**: An owning C++ wrapper over SQLite's native `sqlite3_mutex_alloc`. Mimics `std::mutex` and `std::lock_guard` perfectly, while safely handling `nullptr` mutexes in single-threaded SQLite compilations.
- **`sqlite3_rw_lock`**: A cross-platform Read/Write lock that seamlessly maps to Windows `SRWLOCK`, POSIX `pthread_rwlock_t`, and WASM `memory.atomic.wait32` (via `TinyLock`). Includes zero-overhead C++ RAII wrappers (`SqliteReadGuard` / `SqliteWriteGuard`) to maximize read concurrency while guaranteeing exception-safe locking.

#### Documentation
- [Atomic Quickstart](docs/ATOMIC_README.md)
- [Atomic Architecture](docs/ATOMIC_ARCHITECTURE.md)
- [TinyLock Quickstart](docs/TINY_LOCK_README.md)
- [TinyLock Architecture](docs/TINY_LOCK_ARCHITECTURE.md)
- [Mutex Lock Architecture](docs/MUTEX_LOCK_ARCHITECTURE.md)
- [Read/Write Lock Quickstart](docs/READWRITE_LOCK_README.md)
- [Read/Write Lock Architecture](docs/READWRITE_LOCK_ARCHITECTURE.md)

### 3. Per-Database Shared State Manager (`sqlite3_ext_state.h` / `.hpp`)
Maintaining state (like connection pools, LRU caches, or simple counters) inside a SQLite extension is notoriously difficult due to SQLite's architecture, where extensions are loaded once per process but are used concurrently across multiple database connections.

The state manager solves this by automatically generating a thread-safe, garbage-collected, **Per-Database Shared State Registry**.

#### Key Features:
- **ODR-Safe C API**: Available as a split macro suite (`SQLITE_EXTENSION_STATE_DECLARE` / `DEFINE`) for pure C extensions to perfectly prevent One-Definition Rule violations across multiple translation units.
- **C++ Template API**: Available as a pure C++ template (`SqliteExtState<T>`) for C++ extensions. (Strict compile-time boundaries prevent accidental cross-language misuse).
- **3-Layer Caching Architecture**: Implements O(1) nanosecond-fast state retrieval using SQLite's `sqlite3_set_auxdata` cache, falling back to a global registry.
- **Automated Garbage Collection**: Integrates directly with SQLite's `xDestroy` connection hooks to automatically free memory when the last connection to a database closes.
- **Embedded C++ Objects**: The C++ template seamlessly manages memory lifecycles via `sqlite_new` and `sqlite_delete` (fully relying on standard C++ destructors without needing custom `free_fn` callbacks) to support nested C++ objects (like `std::string`).
- **Lock-Free Ghost Protection**: Uses lock-free atomics (`sqlite_atomic_load`) to power the Double-Checked Locking teardown logic, completely preventing Use-After-Free race conditions during concurrent connection teardowns while avoiding reference counting contention.
- **Cross-Platform Thread Safety**: Native Read/Write locks for Windows (`SRWLOCK`), macOS/Linux (`pthread_rwlock_t`), and WebAssembly (`memory.atomic.wait32` via `TinyLock`).
- **C++ RAII Lock Guards**: The C++ template natively provides `ReadGuard` and `WriteGuard` classes to guarantee exception-safe, scope-based locking.

#### Documentation
- [State Manager Quickstart](docs/EXT_STATE_README.md)
- [State Manager Internal Architecture](docs/EXT_STATE_ARCHITECTURE.md)

### 4. C++ RAII Value Types (`sqlite3_value.hpp`)
Zero-dependency C++ RAII wrappers for SQLite core data types designed for zero-allocation lookups, heterogeneous map keys, UDF argument access, and statement column readings.

#### Key Features:
- **SQLite Integration APIs**: Provides zero-overhead `bind()` and `result()` methods directly on wrappers to easily interoperate with `sqlite3_stmt` parameters and `sqlite3_context` returns.
- **Zero-Allocation Lookups**: Provides non-owning `View` wrappers (`SqliteStringView`, `SqliteBlobView`, `SqliteValueView`) to prevent expensive memory allocations during C++ map key lookups.
- **Small Buffer Optimization (SBO)**: Uses union-based zero-allocation storage for integers and floats, falling back to `sqlite3_value_dup` only for strings and blobs.
- **Heterogeneous Lookups**: Natively supports comparing `View`s against heavy, memory-managed `Owned` classes. Includes 144+ macro-generated operator overloads to instantly compare variants against strings, blobs, and C++ primitives (`int`, `double`) using all 6 standard relational operators (`==`, `!=`, `<`, `>`, `<=`, `>=`).
- **Transparent Map Lookups**: Fully unlocks C++14 `std::less<>` and C++20 `std::unordered_map` heterogeneous lookup capabilities. Query polymorphic maps or hash tables using `my_map.find(5)` or `my_map.find("hello")` natively, without ever instantiating a memory-managed wrapper, thanks to the built-in `SqliteValueHash` and `SqliteValueEqual` functors.
- **Accurate Collation**: Fully conforms to official SQLite collation sorting rules (`NULL < NUMERIC < TEXT < BLOB`), complete with stable `NaN` sorting constraints.
- **Polymorphic Variants**: Safely store Integer, Float, Text, and Blob payloads inside the exact same `std::map` using the polymorphic `SqliteValueOwned` wrapper. Features Strict Weak Ordering tie-breakers to prevent `Int(5)` colliding with `Float(5.0)`.
- **Ergonomic String Builders**: Easily construct dynamic strings without a database handle using standard `(const char*)` constructors, or safely instantiate them inside User-Defined Functions with `(sqlite3_context*)` wrappers.
- **Zero STL Overhead**: Fully implemented using raw C-pointers and SQLite's native memory profilers (`sqlite3_malloc`). No `<string>` or `<vector>` overhead. Perfect for constrained environments like WASM.

#### Documentation
- [Value Types README](docs/VALUE_README.md)
- [Value Types Architecture](docs/VALUE_ARCHITECTURE.md)

### 5. Smart Pointers (`sqlite3_smart_ptr.h` / `.hpp`)
Zero-dependency, thread-safe, reference-counted memory allocation that integrates directly into SQLite's memory manager (`sqlite3_malloc`). Allows safely sharing dynamic payloads across User-Defined Function (UDF) boundaries.

#### Key Features:
- **Zero-Dependency**: Mimics `std::shared_ptr`, `std::unique_ptr`, and `std::weak_ptr` perfectly without linking to `<memory>`.
- **SQLite Memory Profiling**: Allocates exclusively via `sqlite3_malloc` to ensure SQLite accurately tracks heap usage limits (`SQLITE_LIMIT_MEMORY`).
- **100% Lock-Free Ref-Counting**: Utilizes raw explicit memory-barrier atomics (`sqlite3_atomic.h`) to manage strong and weak reference counts. This completely eliminates the heap allocation overhead of a standard `sqlite3_mutex`, resulting in a blistering fast, cache-friendly, 24-byte control block.
- **Custom Deleters**: Natively supports custom destructor callbacks for both the C macro (`Destructor` arg) and the C++ template (`void (*deleter)(T*)`), allowing it to seamlessly manage external memory like `sqlite3_free` or OS-level handles.
- **Dual Support**: Available as a C++ template suite (`SqliteSharedPtr`, `SqliteUniquePtr`, `SqliteWeakPtr`) and as a C macro generation suite (`SQLITE_SHARED_PTR_DEFINE`, `SQLITE_UNIQUE_PTR_DEFINE`) for pure C extensions.
- **Thread-Safe**: Safely passes memory payloads across concurrent UDF calls without race conditions or memory leaks.

#### Documentation
- [Smart Pointers README](docs/SMART_PTR_README.md)
- [Smart Pointers Architecture](docs/SMART_PTR_ARCHITECTURE.md)

### 6. C++ User-Defined Function (UDF) Builder (`sqlite3_udf.hpp`)
A zero-dependency, freestanding C++ framework for registering and executing SQLite User-Defined Functions with zero boilerplate and guaranteed bounds safety.

#### Key Features:
- **Single-Line Registration**: Register C++ scalar functions or stateless lambdas cleanly via `SqliteUdf::define(db, "name", num_args, func)`.
- **Bounds-Checked Argument Proxy**: `SqliteUdfArgs` wraps `(int argc, sqlite3_value** argv)` and ensures out-of-bounds indexing returns safe `SQLITE_NULL` views rather than causing segmentation faults.
- **Zero-Allocation Execution**: Dispatches calls via a lightweight static trampoline proxy using `sqlite3_user_data`, incurring 0 byte heap overhead.
- **Variadic Function Support**: Register dynamic-arity functions (`num_args = -1`) and query `args.size()` dynamically.
- **Deep Synergy with Value Keys**: Fully interoperates with `SqliteValueView`, `SqliteStringOwned`, and heterogeneous operator comparisons (`args[0] == 42`).
- **Deterministic by Default**: Automatically attaches `SQLITE_DETERMINISTIC` to maximize SQLite query optimizer caching.

#### Documentation
- [UDF Builder README](docs/UDF_README.md)
- [UDF Builder Architecture](docs/UDF_ARCHITECTURE.md)

### 7. C++ RAII Statement Wrapper (`sqlite3_statement.hpp`)
A zero-dependency, freestanding C++ RAII wrapper over SQLite prepared statements (`sqlite3_stmt*`) for safe lifetime management, fluent bindings, and zero-allocation column extraction.

#### Key Features:
- **Strict RAII Finalization**: Guarantees statements are safely finalized on scope exit without leaks or exceptions.
- **Move-Only Semantics**: Explicitly deleted copy operations with noexcept move constructor and move assignment for clean lifecycle transfers.
- **Fluent & Named Parameter Bindings**: Type-safe overloads for primitives, strings, blobs, and values by 1-based index or named parameters (`:id`, `@name`, `$val`).
- **Zero-Allocation Column Extraction**: Directly extracts result columns into `SqliteStringView`, `SqliteBlobView`, and `SqliteValueView` without heap copies.
- **Fast Execution Helpers**: Provides `execute()` for atomic step-and-reset on DML/DDL queries, and `next()` for clean multi-row iteration loops.
- **Polymorphic Storage**: Easily extracts column values directly into `SqliteValueOwned` objects for insertion into maps or cache layers.
- **Cached Statement Leasing**: Includes `SqliteCachedStatement` to safely auto-reset statements borrowed from a global cache without finalizing them, completely eliminating `SQLITE_MISUSE` bugs in high-performance loops.

#### Documentation
- [Statement Wrapper README](docs/STATEMENT_README.md)
- [Statement Wrapper Architecture](docs/STATEMENT_ARCHITECTURE.md)
### 8. C++ Aggregate Function Framework (`sqlite3_aggregate.hpp`)
A zero-dependency, type-safe C++ framework for defining SQLite Aggregate Functions using intuitive, object-oriented structs with zero C-pointer casting.

#### Key Features:
- **Object-Oriented Aggregate Structs**: Define aggregation state cleanly as C++ structs with `step()` and `finalize()` methods.
- **Single-Line Registration**: Register aggregates via `SqliteUdf::define_aggregate<T>(db, "name", num_args)` or `SqliteAggregate<T>::define(db, "name", num_args)`.
- **Automatic Return Type Dispatching**: Return primitives (`int`, `sqlite3_int64`, `double`, `bool`), `const char*`, or zero-overhead SQLite wrappers (`SqliteStringOwned`, `SqliteBlobOwned`, `SqliteValueOwned`) directly from `finalize()`.
- **RAII Lifecycle Management**: Employs `sqlite3_aggregate_context` for zero-allocation state tracking with placement construction on the first row and deterministic `~T()` destructor execution upon finalization.
- **Bounds-Safe Parameter Access**: `step(SqliteUdfArgs args)` provides zero-allocation `SqliteValueView` argument access.
- **Empty Set Safety**: Safely finalizes default-constructed instances when aggregating over 0 rows.

#### Documentation
- [Aggregate Functions README](docs/AGGREGATE_README.md)
- [Aggregate Functions Architecture](docs/AGGREGATE_ARCHITECTURE.md)

### 9. C++ Table-Valued Function (TVF) Framework (`sqlite3_tvf.hpp`)
A zero-boilerplate, zero-dependency C++ framework for building Eponymous SQLite Virtual Tables (Table-Valued Functions) by simply implementing a C++ Iterator.

#### Key Features:
- **Zero C-API Boilerplate**: No need to manually define `sqlite3_module` structs or write complex `xConnect`/`xBestIndex`/`xFilter` callbacks. It uses template metaprogramming to build the C-structs statically.
- **Auto-Routing Arguments**: Hidden columns in your schema are automatically routed directly into your `init(args)` method as bounds-safe `SqliteUdfArgs`.
- **Query Planner Auto-Tuning**: Automatically handles SQLite's `xBestIndex` constraint logic to guarantee the optimizer passes the maximum number of arguments to your function, enabling perfect Correlated Subqueries.
- **Custom Cost Override**: Cleanly override the query planner `estimatedCost` heuristic using static C++ name-hiding without any virtual vtable overhead.
- **Zero VTable Overhead**: The framework intentionally avoids virtual destructors, downcasting in `xClose` to ensure complete `-nostdlib++` safety without requiring a global `operator delete`.

#### Documentation
- [TVF Framework README](docs/TVF_README.md)
- [TVF Framework Architecture](docs/TVF_ARCHITECTURE.md)

### 10. Exception-Safe Transactions (`sqlite3_transaction.hpp`)
A zero-dependency C++ RAII wrapper for SQLite Transactions and Savepoints that prevents database locking bugs by guaranteeing automatic rollbacks on scope exit.

#### Key Features:
- **Strict RAII Lifecycle**: Destructors automatically execute `ROLLBACK` if the transaction is still active, protecting against C++ exceptions and early returns.
- **Nested Transactions**: Full support for nested SQLite transactions using the `SqliteSavepoint` wrapper (`SAVEPOINT`, `RELEASE`, `ROLLBACK TO`).
- **Zero Allocations**: Uses `sqlite3_mprintf` to safely construct queries and escape savepoint identifiers without pulling in `<string>`.
- **Multiple Behaviors**: Supports `DEFERRED` (default), `IMMEDIATE`, and `EXCLUSIVE` transaction locks.

#### Documentation
- [Transaction Wrapper README](docs/TRANSACTION_README.md)
- [Transaction Architecture Guide](docs/TRANSACTION_ARCHITECTURE.md)

### 11. Database Connection Lifecycle (`sqlite3_db.hpp`)
A clean, object-oriented RAII wrapper for managing SQLite connection handles and rapidly building query statements.

#### Key Features:
- **Owned vs View Patterns**: `SqliteDatabaseOwned` safely manages `sqlite3_open_v2` and `sqlite3_close_v2`, while `SqliteDatabaseView` provides zero-cost C++ wrappers over existing C-API handles.
- **Statement Builders**: Integrates seamlessly with Statements, Transactions, and Savepoints. You can directly call `.prepare()` or `.prepare_cached()` on any Database, Transaction, or Savepoint object to instantly generate a safe `SqliteStatement` without juggling raw pointers.

#### Documentation
- [Database Wrapper README](docs/DB_README.md)
- [Database Architecture Guide](docs/DB_ARCHITECTURE.md)

## Building and Testing
This repository includes a robust test suite to verify the thread-safety and memory-safety of the extensions under immense load.

To run the integration tests across both the C and C++ extensions:
```bash
make test
```
