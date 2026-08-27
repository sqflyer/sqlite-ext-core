# Systems Architecture: Coroutine Table-Valued Functions (`sqlite3_tvf_coro.hpp`)

This document details the systems-level engineering and compile-time metaprogramming architecture of the **Coroutine Table-Valued Function (TVF) Framework** (`sqlite3_tvf_coro.hpp`). It specifies how generator coroutines are lowered into SQLite virtual tables (`sqlite3_module`), how column output types are statically multiplexed, and how memory safety is guaranteed under `-nostdlib++` and `-fno-exceptions`.

> **Developer Guide & Tutorials**: For quickstarts, feature comparisons, and SQL usage patterns, see [`docs/TVF_CORO_README.md`](TVF_CORO_README.md).

---

## 1. Architectural Motivation & Design Invariants

Traditional SQLite Table-Valued Functions (TVFs) require implementing a 5-method C++ iterator class or a 23-callback `sqlite3_module` struct. This introduces severe design friction:
1. **Control Flow Inversion**: The developer cannot write simple `for` loops or recursive functions because `xNext()` must pause after emitting every single row.
2. **State Splitting**: Variables that would normally be local stack variables (`int i`, `sqlite3_int64 current`, `char* ptr`) must be promoted to member variables on a heap-allocated cursor struct.
3. **Manual Column Indexing**: In `xColumn(ctx, i)`, the developer must write fragile `switch (i)` branches to route data to the appropriate output column.

`sqlite3_tvf_coro.hpp` solves all three issues:
- **Natural Procedural Execution**: The developer writes a single sequential `generate(SqliteUdfArgs)` function.
- **Unified Generator Support**: Supports both **Stackful Fibers (`SqliteFiberGenerator<T>`)** for recursive tree traversals and **Stackless C++20 Coroutines (`SqliteGenerator<T>`)** for flat loops in ~48-byte frames.
- **Static Column Multiplexing**: Automatically dispatches scalar values, `SqliteValueOwned`, `SqliteRowStatic<N>`, and `SqliteRowDynamic` to the SQLite context without manual `switch (i)` boilerplate.

---

## 2. Compile-Time Type Deduction Pipeline

`SqliteTvfCoroModule<T>` uses zero-dependency compile-time metaprogramming to deduce the exact generator type and yielded value type without `<type_traits>` or standard library headers:

```
 User Struct T::generate(args)
 ──────────────┬──────────────
               │
               ▼
   decltype(T::generate(SqliteUdfArgs(0, nullptr)))
               │
               ├─────────────────────────► GeneratorType (e.g. SqliteFiberGenerator<SqliteRowStatic<3>>)
               │
               ▼
   sqlite_declval<GeneratorType>().value()
               │
               ▼
   sqlite_remove_cv<decltype(...)>::type
               │
               └─────────────────────────► ValueType     (e.g. SqliteRowStatic<3>)
```

### Implementation:
```cpp
template <typename T>
struct SqliteTvfCoroModule {
    // 1. Deduce the exact generator type returned by the user's static generate() function:
    typedef decltype(T::generate(SqliteUdfArgs(0, nullptr))) GeneratorType;

    // 2. Strip const and reference qualifiers from the generator's yielded value type:
    typedef typename sqlite_remove_cv<decltype(sqlite_declval<GeneratorType>().value())>::type ValueType;
    // ...
};
```

---

## 3. Polymorphic Column Multiplexing (`SqliteTvfColumnWriter`)

When SQLite invokes `xColumn(cur, ctx, col_idx)`, the engine requests the value of column `col_idx` for the current row. `SqliteTvfColumnWriter<ValueType>` uses compile-time template specialization to route the data:

```
                            xColumn(cur, ctx, col_idx)
                                        │
                                        ▼
                        SqliteTvfColumnWriter<ValueType>
                                        │
        ┌───────────────────────────────┼───────────────────────────────┐
        ▼                               ▼                               ▼
 [Scalar Types]                 [Static Rows]                   [Dynamic Rows]
 (int, int64, double, string)   (SqliteRowStatic<N>)            (SqliteRowDynamic)
        │                               │                               │
        ▼                               ▼                               ▼
 if (col_idx == 0)              if (col_idx < N)                if (col_idx < row.size())
   ctx.result_*(val)              row[col_idx].result(ctx)        row[col_idx].result(ctx)
```

### Supported Specializations:
1. **Primitives**: `sqlite3_int64`, `int`, `double`, `const char*`.
2. **String Views**: `SqliteStringView` (direct sized pointer transfer without null-termination allocations).
3. **Owned Polymorphic Values**: `SqliteValueOwned` (`val.result(ctx.get())`).
4. **Fixed-Size Static Row Arrays**: `SqliteRowStatic<N>` (indexed `row[col_idx].result(ctx.get())` with bounds checking).
5. **Dynamic Row Buffers**: `SqliteRowDynamic` (multi-column dynamic arrays).

---

## 4. SQLite Virtual Table C-API Callback Mapping

`SqliteTvfCoroModule<T>` generates a static `sqlite3_module` definition that maps SQLite's C callbacks to the generator lifecycle:

```
   SQL Query Step             SQLite VDBE Callback             Generator Action
────────────────────        ───────────────────────          ────────────────────
1. Connection open      ──►   xConnect()             ──►      Declare schema & allocate VTab
2. Query plan choice    ──►   xBestIndex()           ──►      Map hidden arguments to argv
3. Cursor open          ──►   xOpen()                ──►      Allocate Cursor control block
4. Query execute        ──►   xFilter(argc, argv)    ──►      T::generate(args) & instantiate Gen
5. Check if done        ──►   xEof()                 ──►      gen->is_done()
6. Fetch column data    ──►   xColumn(ctx, i)        ──►      ColumnWriter::write(ctx, gen->value(), i)
7. Advance to next row  ──►   xNext()                ──►      gen->next(), rowid++
8. Loop 5..7 until EOF
9. Cursor close         ──►   xClose()               ──►      Destroy Generator & free Cursor
10. Connection close    ──►   xDisconnect()          ──►      Free VTab
```

---

## 5. Cursor Memory Layout & 1-Cycle Move Semantics

A query cursor manages the lifetime of a heap-allocated generator instance allocated strictly via `sqlite3_malloc64`:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          CURSOR CONTROL BLOCK (Cursor)                      │
│   sqlite3_vtab_cursor base;                                                 │
│   GeneratorType*      gen;     ──────────┐                                  │
│   sqlite3_int64       rowid;             │                                  │
└──────────────────────────────────────────┼──────────────────────────────────┘
                                           │
                                           ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                          GENERATOR INSTANCE (Heap)                          │
│                                                                             │
│   [Stackful Fiber Generator]                 [Stackless C++20 Generator]    │
│   • SqliteCoroutine (Fiber handle)           • Coroutine promise handle     │
│   • Dedicated execution stack (16–64KB)      • Compact frame (~48 bytes)    │
│   • Cached current value: ValueType          • Cached current value         │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Move Semantics in `xFilter`:
When a query executes, `T::generate(args)` returns an rvalue generator. `xFilter` moves this generator into heap storage via `sqlite_move` in 1 CPU cycle:
```cpp
SqliteUdfArgs args(argc, argv);
GeneratorType gen_instance = T::generate(args);
pCur->gen = sqlite_new<GeneratorType>(sqlite_move(gen_instance));
```

---

## 6. Shared State Injection Architecture (`SqliteExtState`)

For TVFs that read or modify connection-level shared state (e.g. caches, thread pools, metrics counters), `SqliteTvfCoro::define_with_state<State, T>` binds shared state seamlessly:

```
                            sqlite3_create_module_v2
                                        │
                     pClientData = raw_state (SqliteExtState)
                                        │
                                        ▼
                             xConnect(db, pAux, ...)
                                        │
                         pTab->raw_state = pAux
                                        │
                                        ▼
                              xColumn(cur, ctx, i)
                                        │
               SqliteContext sqlite_ctx(ctx, pTab->raw_state)
                                        │
                                        ▼
               O(1) Direct Access via sqlite_ctx.state<State>()
```

---

## 7. Dual Engine Architectural Comparison

| Architectural Property | Stackful Fiber TVF (`SqliteFiberGenerator<T>`) | Stackless C++20 TVF (`SqliteGenerator<T>`) |
| :--- | :--- | :--- |
| **Stack Allocation** | Dedicated 16KB–64KB stack via `sqlite3_malloc64` | **0 Bytes (Compiler-generated frame)** |
| **Frame Memory Size** | Stack size + ~80 bytes state control block | **~32 – 64 Bytes total** |
| **Deep Call Depth Yield**| **Supported** (Yield from recursive subroutines) | **Unsupported** (Top-level loop only) |
| **Context Switch Time** | **~15 – 25 ns** (Hardware register swap) | **~1 – 3 ns** (Direct function jump) |
| **Compiler Requirement**| **C++11** (`-std=c++11`) | **C++20** (`-std=c++20` or `/std:c++20`) |
| **Standard Library Dep**| **0.0% (`-nostdlib++`)** | **0.0% (`-nostdlib++`)** |
