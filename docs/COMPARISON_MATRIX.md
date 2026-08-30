# Complete C++ Type Comparison Matrix (`sqlite-ext-core`)

An exhaustive architectural reference and comparative matrix across all types, containers, views, macros, and primitives in `sqlite-ext-core`.

---

## 1. Value & Primitive Types Matrix (`sqlite3_value.hpp`)

| Type | Size | Ownership | Storage / Allocation | SBO Capacity | Subtype Preservation | Affinity Support | Transparent Functors | Primary Use Case |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`SqliteValueView`** | 8 Bytes | Non-Owning | Stack / Register (wraps `const sqlite3_value*`) | N/A | Zero-copy inspection | Derived from storage | `SqliteValueHash`<br>`SqliteValueEqual`<br>`SqliteValueLess` | Reading UDF arguments (`argv[i]`) and statement columns with 0 allocations. |
| **`SqliteValueOwned`** | 16 Bytes | Owning (RAII) | In-Situ Dual Union / Heap (`sqlite3_value_dup`) | Strings $\le 13$B<br>Blobs $\le 14$B | Offset 14 (Zero-branch) | Native byte (Offset 12) | `SqliteValueHash`<br>`SqliteValueEqual`<br>`SqliteValueLess` | General-purpose SQLite value storage, map keys, accumulator state, and cache entries. |
| **`SqliteStringView`** | 16 Bytes | Non-Owning | Pointer (`const char*`) + 4B length | N/A | N/A | Text (`'B'`) | `SqliteRowHash`<br>`SqliteValueHash` | Transient text inspection and zero-allocation map lookups. |
| **`SqliteStringOwned`** | 8 Bytes | Owning (RAII) | Heap dynamic string builder (`sqlite3_str*`) | Dynamic growth | N/A | Text (`'B'`) | `SqliteRowHash`<br>`SqliteValueHash` | High-efficiency dynamic string concatenation and formatting for UDF results. |
| **`SqliteBlobView`** | 16 Bytes | Non-Owning | Pointer (`const void*`) + 4B length | N/A | N/A | Blob (`'A'`) | `SqliteRowHash`<br>`SqliteValueHash` | Non-owning binary data inspection (vectors, serialized structs, images). |
| **`SqliteBlobOwned`** | 16 Bytes | Owning (RAII) | Dynamic heap buffer (`sqlite3_malloc64`) | Dynamic | N/A | Blob (`'A'`) | `SqliteRowHash`<br>`SqliteValueHash` | Owned binary payload lifecycle management with explicit byte size tracking. |

---

## 2. Value Containers & Row Views Matrix (`sqlite3_value_containers.hpp`, `sqlite3_row.hpp`)

| Container Type | Memory Size | Ownership | Storage Mechanism | SBO Capacity | Cache Density (64B Line) | Range-Based `for` Iterator | Transparent STL Functors | Primary Use Case |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`SqliteValueTuple<N>`** ($N \in [1..8]$) | **$N \times 16$ Bytes** | Owning (RAII) | Exact in-situ Stack Array | Full Stack ($N$ columns) | $4/N$ tuples / line | `for (const auto& v : tuple)` | `SqliteRowHash`<br>`SqliteRowEqual`<br>`SqliteRowLess` | Fixed-arity Primary Keys, Composite Index Keys, and Fixed-Schema Records ($0\text{ mallocs}$). |
| **`SqliteValueTuple<0>`** (`SqliteValueTuple<>`) | **16 Bytes** | Owning (RAII) | Direct Heap Tuple (`sqlite3_malloc64`) | 0 (All on heap) | 4 handles / line | `for (const auto& v : tuple)` | `SqliteRowHash`<br>`SqliteRowEqual`<br>`SqliteRowLess` | Dynamic fixed-width records where size is passed as constructor argument. |
| **`SqliteValueVec<N>`** ($N \in [1..8]$) | **$N \times 16$ Bytes** | Owning (RAII) | Adaptive In-Situ Stack SBO $\le N$, Spills to Heap if $> N$ | Up to $N$ columns in-situ | $4/N$ vectors / line | `for (const auto& v : vec)` | `SqliteRowHash`<br>`SqliteRowEqual`<br>`SqliteRowLess` | Dynamic payload rows, non-PK value columns, variable-length scratch vectors with reversible stack/heap lifecycle. |
| **`SqliteValueVec<0>`** (`SqliteValueVec<>`) | **16 Bytes** | Owning (RAII) | Direct Heap Dynamic Vector (`sqlite3_malloc64`) | 0 (All on heap) | 4 handles / line | `for (const auto& v : vec)` | `SqliteRowHash`<br>`SqliteRowEqual`<br>`SqliteRowLess` | Unbounded runtime variable-column payload records. |
| **`SqliteRowOwnedWrapper`** | **16 Bytes** | Non-Owning | 8B Pointer (`SqliteValueOwned*`) + 4B Length | N/A (Spans any contiguous buffer) | **4 spans / line** | `for (const auto& v : span)` | `SqliteRowHash`<br>`SqliteRowEqual`<br>`SqliteRowLess` | Zero-allocation non-owning span parameter over any `SqliteValueOwned` contiguous sequence. |
| **`SqliteRowView`** (`SqliteUdfArgs`) | **24 Bytes** | Non-Owning | Tagged union (`sqlite3_stmt*`, `sqlite3_value**`, `SqliteValueView*`) + Length | N/A (Universal view) | 2 rows / line | `for (SqliteValueView col : row)` | `SqliteRowHash`<br>`SqliteRowEqual`<br>`SqliteRowLess` | Zero-allocation inspection over prepared statement step rows, UDF `argv` arguments, and view arrays. |

---

## 3. Relational Comparison Matrix (Direct Operators: `==`, `!=`, `<`, `<=`, `>`, `>=`)

All comparisons are macro-derived, symmetric, zero-allocation, and strictly conform to SQLite's type collation sorting hierarchy:

$$\text{NULL} (0) < \text{NUMERIC} (1) < \text{TEXT} (2) < \text{BLOB} (3)$$

| Left \ Right | `SqliteValueOwned` | `SqliteValueView` | `SqliteRowView` | `SqliteValueTuple<N>` | `SqliteValueVec<N>` | `SqliteRowOwnedWrapper` | Primitives (`int`, `int64`, `double`, `bool`, `const char*`) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **`SqliteValueOwned`** | Full (Symmetric) | Full (Symmetric) | Full (1-Column) | Full (1-Column) | Full (1-Column) | Full (1-Column) | Full (Symmetric) |
| **`SqliteValueView`** | Full (Symmetric) | Full (Symmetric) | Full (1-Column) | Full (1-Column) | Full (1-Column) | Full (1-Column) | Full (Symmetric) |
| **`SqliteRowView`** | Full (1-Column) | Full (1-Column) | Full (Lexicographical) | Full (Symmetric) | Full (Symmetric) | Full (Symmetric) | Full (1-Column) |
| **`SqliteValueTuple<N>`** | Full (1-Column) | Full (1-Column) | Full (Symmetric) | Full (Lexicographical) | Full (Symmetric) | Full (Symmetric) | Full (1-Column) |
| **`SqliteValueVec<N>`** | Full (1-Column) | Full (1-Column) | Full (Symmetric) | Full (Symmetric) | Full (Lexicographical) | Full (Symmetric) | Full (1-Column) |
| **`SqliteRowOwnedWrapper`** | Full (1-Column) | Full (1-Column) | Full (Symmetric) | Full (Symmetric) | Full (Symmetric) | Full (Lexicographical) | Full (1-Column) |

---

## 4. Macro Synthesis Suite Matrix (`docs/MACROS.md`)

This matrix details which macro synthesizers are implemented by each class:

| Class | `ARRAY_ACCESSORS` | `ARRAY_HASH` | `ARRAY_ITERATOR` | `CONTAINER_RELATIONAL_OPS` | `ALL_SCALAR_OPS` | `ALL_REVERSE_OPS` | Transparent Functors |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **`SqliteValueOwned`** | N/A | Member | N/A | `SQLITE_DEF_VAL_*_OPS` | Inlined | Inlined | `SqliteValueHash`, `SqliteValueEqual`, `SqliteValueLess` |
| **`SqliteValueView`** | N/A | Member | N/A | `SQLITE_DEF_VAL_*_OPS` | Inlined | Inlined | `SqliteValueHash`, `SqliteValueEqual`, `SqliteValueLess` |
| **`SqliteRowView`** | **Yes** | **Yes** | **Yes** | **Yes** | **Yes** | **Yes** | `SqliteRowHash`, `SqliteRowEqual`, `SqliteRowLess` |
| **`SqliteRowOwnedWrapper`** | **Yes** | **Yes** | **Yes** | **Yes** | **Yes** | **Yes** | `SqliteRowHash`, `SqliteRowEqual`, `SqliteRowLess` |
| **`SqliteValueTuple<N>`** | **Yes** | **Yes** | **Yes** | **Yes** | **Yes** | **Yes** | `SqliteRowHash`, `SqliteRowEqual`, `SqliteRowLess` |
| **`SqliteValueVec<N>`** | **Yes** | **Yes** | **Yes** | **Yes** | **Yes** | **Yes** | `SqliteRowHash`, `SqliteRowEqual`, `SqliteRowLess` |

---

## 5. Buffers, Strings & Streams Matrix (`sqlite3_buffer.hpp`, `sqlite3_blob_stream.hpp`)

| Type | Ownership / Allocation | In-Place Expansion | Exception Safety | Zero-Copy Output | Interop with SQLite / Primary Role |
| :--- | :--- | :---: | :---: | :---: | :--- |
| **`SqliteBufferSlice`** | **Non-Owning (16B)** (Pointer + `int64` bytes) | N/A (Span view) | `-fno-exceptions` safe | Direct pointer inspection | Freestanding replacement for `std::span<const uint8_t>` and `std::string_view`. |
| **`SqliteBuffer`** | `sqlite3_malloc64` / `sqlite3_realloc64` | Yes (2x growth) | `-fno-exceptions` safe | Pointer transfer (`release()`) | Direct binding to SQLite blobs; replaces `std::vector<uint8_t>`. |
| **`SqliteString`** | `sqlite3_malloc64` / `sqlite3_realloc64` | Yes (2x growth) | `-fno-exceptions` safe | Null-terminated `c_str()` | Direct binding to SQLite text; replaces `std::string`. |
| **`SqliteBlobStream`** | SQLite Incremental Blob (`sqlite3_blob*`) | Chunk-based streaming | `SQLITE_BUSY` safe | Zero-allocation read/write | Direct row/column streaming with incremental chunk offsets. |

---

## 6. Database, Statement & Transaction Matrix (`sqlite3_db.hpp`, `sqlite3_statement.hpp`, `sqlite3_transaction.hpp`)

| Type | Handle Wrapper | Ownership Model | RAII Finalizer | Reentrancy / State Safety | Primary Responsibility |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **`SqliteDatabaseView`** | `sqlite3*` | Non-Owning | None | Safe | Borrowed database connection handle for extensions. |
| **`SqliteDatabaseOwned`** | `sqlite3*` | Owning (Exclusive) | `sqlite3_close_v2` | Closes on destruction | Standalone database connection lifecycle. |
| **`SqliteStatement`** | `sqlite3_stmt*` | Owning / Non-Owning | `sqlite3_finalize` | Step iterator & column binder | Prepared statement execution and query iteration. |
| **`SqliteTransaction`** | `sqlite3*` | Owning (Scope) | `ROLLBACK` on scope exit | Savepoint / Nested safe | RAII transaction management with automatic rollback. |

---

## 7. Smart Pointers & Allocators Matrix (`sqlite3_smart_ptr.hpp`, `sqlite3_allocator.hpp`)

| Type | Reference Counted | Allocator | Custom Deleter | STL Container Compatible | Thread Safety |
| :--- | :---: | :--- | :---: | :---: | :--- |
| **`SqliteUniquePtr<T>`** | No (Unique) | `sqlite3_malloc64` / `sqlite3_free` | Supported (`DefaultDelete<T>`) | Move-only | Single-threaded |
| **`SqliteSharedPtr<T>`** | Yes (Atomic) | `sqlite3_malloc64` / `sqlite3_free` | Supported | Full Copy/Move | Multi-threaded reference count |
| **`SqliteAllocator<T>`** | N/A | `sqlite3_malloc64` / `sqlite3_free` | N/A | **Yes (`std::allocator_traits`)** | Process-wide SQLite memory pool |

---

## 8. Synchronization & Concurrency Matrix (`sqlite3_mutex_lock.hpp`, `sqlite3_tiny_lock.hpp`, `sqlite3_readwrite_lock.hpp`, `sqlite3_atomic.hpp`)

| Synchronization Type | Memory Footprint | OS Primitive / Mechanism | Reentrant / Recursive | Read/Write Split | Contention Strategy |
| :--- | :---: | :--- | :---: | :---: | :--- |
| **`SqliteTinyLock`** | **1 Byte** | Atomic Spinlock (`std::atomic_flag` / CAS) | No | No | Low-overhead short-critical-section spin |
| **`SqliteMutexLock`** | **8 Bytes** | `sqlite3_mutex*` (OS Native Mutex) | Yes (Fast / Recursive) | No | OS-level thread sleep / wakeup |
| **`SqliteReadWriteLock`** | **8 Bytes** | Dual Atomic State Registers | Read-reentrant | **Yes (Shared / Exclusive)** | High-throughput concurrent reader lock |
| **`SqliteAtomic<T>`** | **sizeof(T)** | Hardware atomic instructions | Lock-free | N/A | Lock-free scalar state updates |

---

## 9. Extension Extensibility Matrix (`sqlite3_udf.hpp`, `sqlite3_tvf.hpp`, `sqlite3_vtab.hpp`, `sqlite3_vtab_arg.hpp`, `sqlite3_dispatch_8x8.hpp`)

| Extensibility Mechanism | Component Class | State Binding | Lifecycle Management | Result Delivery | Use Case |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Scalar UDF** | `SqliteUdf` / `SqliteExt::define_udf` | Stateless or `SqliteExtState<T>` | Per-call invocation | `ctx.result(...)` | Point transformations, math, text processing. |
| **Aggregate Function** | `SqliteAggregate` | `SqliteAggregateContext<T>` | `step()` per row, `final()` at group end | `ctx.result(...)` | Group accumulators, statistics, hashing. |
| **Table-Valued Function (TVF)** | `SqliteTvf` | In-memory row cursor | Row generator iteration | Column population | Dynamic row-generating functions. |
| **Coroutine TVF (M:N)** | `SqliteTvfCoro` | Fiber / Coroutine State | Cooperative `co_yield` | Multi-threaded Worker Pool | Asynchronous I/O, heavy computation, streaming. |
| **Virtual Table (Full)** | `SqliteVTable` | Module + VTable + Cursor | Full xCreate/xConnect/xFilter/xUpdate | Direct SQLite VDBE integration | Custom storage engines, indexes, external APIs. |
| **VTab Argument Parser** | `SqliteVTabArgs` / `SqliteVTabArg` | Stateless Argument Reflection | Parser & Validator | `args.validate()`, `args.pk_count()` | Structured parsing of `argv` parameters, column schemas, and multi-PK definitions. |
| **8x8 Matrix Dispatcher** | `SQLITE_MAKE_DEFAULT_STORAGE_8X8` | Compile-Time Template Matrix | Dynamic Factory Instantiation | Storage Pointer | $8 \times 8 = 64$ compile-time template specializations based on runtime PK and Value counts. |

---

## 10. Transparent STL & Swiss Table Functor Matrix

| Container Type | Transparent Hash Functor | Transparent Equality Functor | Transparent Ordering Functor | Supported Key Search Types (Zero Allocation) |
| :--- | :--- | :--- | :--- | :--- |
| **`std::unordered_map<SqliteValueOwned, T>`** | `SqliteValueHash` | `SqliteValueEqual` | N/A | `SqliteValueOwned`, `SqliteValueView`, `SqliteStringView`, `SqliteBlobView`, `int`, `sqlite3_int64`, `double`, `bool`, `const char*` |
| **`std::map<SqliteValueOwned, T>`** | N/A | N/A | `SqliteValueLess` | `SqliteValueOwned`, `SqliteValueView`, `SqliteStringView`, `SqliteBlobView`, `int`, `sqlite3_int64`, `double`, `bool`, `const char*` |
| **`std::unordered_map<SqliteValueTuple<N>, T>`**| `SqliteRowHash` | `SqliteRowEqual` | N/A | `SqliteValueTuple<N>`, `SqliteValueVec<N>`, `SqliteRowOwnedWrapper`, `SqliteRowView`, `SqliteValueOwned`, `SqliteValueView`, `SqliteStringView`, `SqliteBlobView`, primitives |
| **`std::map<SqliteValueTuple<N>, T>`** | N/A | N/A | `SqliteRowLess` | `SqliteValueTuple<N>`, `SqliteValueVec<N>`, `SqliteRowOwnedWrapper`, `SqliteRowView`, `SqliteValueOwned`, `SqliteValueView`, `SqliteStringView`, `SqliteBlobView`, primitives |
| **`std::unordered_map<SqliteValueVec<N>, T>`** | `SqliteRowHash` | `SqliteRowEqual` | N/A | `SqliteValueVec<N>`, `SqliteValueTuple<N>`, `SqliteRowOwnedWrapper`, `SqliteRowView`, `SqliteValueOwned`, `SqliteValueView`, `SqliteStringView`, `SqliteBlobView`, primitives |
| **`std::map<SqliteValueVec<N>, T>`** | N/A | N/A | `SqliteRowLess` | `SqliteValueVec<N>`, `SqliteValueTuple<N>`, `SqliteRowOwnedWrapper`, `SqliteRowView`, `SqliteValueOwned`, `SqliteValueView`, `SqliteStringView`, `SqliteBlobView`, primitives |
| **`std::unordered_map<SqliteRowOwnedWrapper, T>`**| `SqliteRowHash` | `SqliteRowEqual` | N/A | `SqliteRowOwnedWrapper`, `SqliteValueTuple<N>`, `SqliteValueVec<N>`, `SqliteRowView`, `SqliteValueOwned`, primitives |
| **`std::map<SqliteRowOwnedWrapper, T>`** | N/A | N/A | `SqliteRowLess` | `SqliteRowOwnedWrapper`, `SqliteValueTuple<N>`, `SqliteValueVec<N>`, `SqliteRowView`, `SqliteValueOwned`, primitives |
| **`std::unordered_map<SqliteStringOwned, T>`** | `SqliteValueHash` | `SqliteValueEqual` | N/A | `SqliteStringOwned`, `SqliteStringView`, `const char*` |
| **`std::map<SqliteStringOwned, T>`** | N/A | N/A | `SqliteValueLess` | `SqliteStringOwned`, `SqliteStringView`, `const char*` |
