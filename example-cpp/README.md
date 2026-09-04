# Comprehensive Guide: SQLite C++ Extension Example

This directory contains a complete, production-ready demonstration of building, compiling, loading, and querying a native loadable SQLite extension developed in modern C++11 using **`sqlite-ext-core`**.

---

## Table of Contents
1. [Overview & Philosophy](#1-overview--philosophy)
2. [Directory Structure](#2-directory-structure)
3. [Deep-Dive Code Walkthrough](#3-deep-dive-code-walkthrough)
   - [3.1 Connection Shared State (`AnalyticsState`)](#31-connection-shared-state-analyticsstate)
   - [3.2 Stateless Scalar UDF (`math_hypot`)](#32-stateless-scalar-udf-math_hypot)
   - [3.3 Fallible String UDF (`text_repeat`)](#33-fallible-string-udf-text_repeat)
   - [3.4 Stateful Scalar UDF (`analytics_ping`)](#34-stateful-scalar-udf-analytics_ping)
   - [3.5 Object-Oriented Aggregate (`geo_mean`)](#35-object-oriented-aggregate-geo_mean)
   - [3.6 Table-Valued Function (`fibonacci`)](#36-table-valued-function-fibonacci)
   - [3.7 Unified Entrypoints & Facade Registration](#37-unified-entrypoints--facade-registration)
4. [Compilation Architecture & Build Commands](#4-compilation-architecture--build-commands)
5. [Running the Interactive Demo](#5-running-the-interactive-demo)
6. [Expected Terminal Output & Verification](#6-expected-terminal-output--verification)
7. [Multi-Language Integration Guides](#7-multi-language-integration-guides)
   - [Python](#71-python)
   - [Node.js (`better-sqlite3`)](#72-nodejs-better-sqlite3)
   - [C++ Host Application](#73-c-host-application)
8. [Critical Invariants & Best Practices](#8-critical-invariants--best-practices)

---

## 1. Overview & Philosophy

SQLite extensions are dynamically compiled shared libraries (`.so` on Linux, `.dll` on Windows, `.dylib` on macOS) that extend SQLite's SQL engine at runtime. 

Traditional C extensions require tedious, error-prone boilerplate:
- Manually allocating and binding routine dispatch tables (`sqlite3_api`).
- Wrapping entrypoints in `extern "C"` and platform decorators (`__declspec(dllexport)` / `__attribute__((visibility("default")))`).
- Manual `void*` memory casting and explicit mutex management.

**`sqlite-ext-core`** provides [`include/sqlite3_ext_creator.hpp`](../include/sqlite3_ext_creator.hpp) and [`include/sqlite3_ext.hpp`](../include/sqlite3_ext.hpp) to eliminate this boilerplate entirely while maintaining **strict freestanding constraints** (`-nostdlib++`, `-fno-exceptions`, `-fno-rtti`) and zero runtime overhead.

---

## 2. Directory Structure

```
example-cpp/
├── build/                      <-- Created during build (contains libexample.dll / .so)
├── example.cpp                 <-- Complete C++ extension implementation
├── example.sql                 <-- SQL test script exercising all 5 subsystems
├── Makefile                    <-- Automated build & execution Makefile
└── README.md                   <-- This comprehensive documentation guide
```

---

## 3. Deep-Dive Code Walkthrough

The example extension ([`example.cpp`](example.cpp)) demonstrates all core extensibility pillars in SQLite:

### 3.1 Connection Shared State (`AnalyticsState`)

Extensions often require persistent connection state (e.g., query counters, LRU caches, session tags) that live across multiple query executions on the same database connection:

```cpp
struct AnalyticsState {
    int total_queries;
    double running_sum;
    char session_tag[64];
};
```

- **Thread Safety & Pluggable Lock Policies**: `SqliteExtState<AnalyticsState, LockPolicy>` manages thread safety with zero overhead:
  - **`SqliteRwLock` (Default)**: `SqliteExtState<AnalyticsState>` / `SqliteExtStateRw<AnalyticsState>` — Ideal for read-heavy workloads with concurrent `ReadGuard` and exclusive `WriteGuard`.
  - **`SqliteTinyLock` (1-Byte Spinlock)**: `SqliteExtStateTiny<AnalyticsState>` — Recommended for micro-states, fast in-memory key-value stores (`memkv`), and atomic counters with only 1 byte memory footprint.
  - **`SqliteMutex` (SQLite Native)**: `SqliteExtStateMutex<AnalyticsState>` — Delegates locking directly to SQLite's native `sqlite3_mutex_alloc`.
- **Connection Isolation**: Separate database connections (e.g., separate `:memory:` databases or threads) receive completely independent instances of `AnalyticsState`.

---

### 3.2 Stateless Scalar UDF (`math_hypot`)

A pure scalar SQL function that computes the hypotenuse $\sqrt{a^2 + b^2}$:

```cpp
static void math_hypot(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() < 2) {
        ctx.result_error("math_hypot requires 2 numeric arguments");
        return;
    }
    double a = args[0].as_double();
    double b = args[1].as_double();
    
    // Freestanding square-root via Newton-Raphson approximation
    double sq = a * a + b * b;
    if (sq <= 0.0) {
        ctx.result_double(0.0);
        return;
    }
    double root = sq / 2.0;
    for (int i = 0; i < 20; ++i) {
        root = 0.5 * (root + sq / root);
    }
    ctx.result_double(root);
}
```

- `args[0]` and `args[1]` provide bounds-safe, zero-allocation access to SQLite argument values (`SqliteValueView`).
- `ctx.result_double(root)` safely forwards the computed floating-point value to SQLite's execution engine.

---

### 3.3 Fallible String UDF (`text_repeat`)

Demonstrates fallible memory allocation and dynamic string buffer manipulation with Rust-style `SqliteResult<SqliteString>` and `SqliteStatus`:

```cpp
static void text_repeat(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() < 2) {
        ctx.result_error("text_repeat requires (text, count)");
        return;
    }
    SqliteStringView text = args[0].as_text();
    int count = static_cast<int>(args[1].as_int64());
    if (count < 0) count = 0;

    // Fallible buffer allocation returning SqliteResult<SqliteString>
    auto res_buf = SqliteString::try_create();
    if (res_buf.is_err()) {
        res_buf.set_sqlite_err(ctx.get());
        return;
    }
    SqliteString str = res_buf.take_value();
    SqliteStatus reserve_stat = str.try_reserve(text.length() * count + 1);
    if (reserve_stat.is_err()) {
        reserve_stat.set_sqlite_err(ctx.get());
        return;
    }
    for (int i = 0; i < count; ++i) {
        SqliteStatus stat = str.try_append(text.data(), text.length());
        if (stat.is_err()) {
            stat.set_sqlite_err(ctx.get());
            return;
        }
    }
    ctx.result_text(str.c_str(), str.length());
}
```

- `SqliteString::try_create()` and `try_reserve()` gracefully propagate out-of-memory errors via `res.set_sqlite_err(ctx.get())`.
- `SqliteString::try_append()` dynamically expands the buffer without throwing exceptions or risking null-pointer dereferences.

---

### 3.4 Stateful Scalar UDF (`analytics_ping`)

A stateful scalar function that mutates and returns the per-connection query counter:

```cpp
static void analytics_ping(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    AnalyticsState* state = ctx.state<AnalyticsState>();
    if (!state) {
        ctx.result_error("AnalyticsState not initialized");
        return;
    }
    int count = 0;
    {
        SqliteExtState<AnalyticsState>::WriteGuard lock(state);
        lock->total_queries++;
        count = lock->total_queries;
    }
    ctx.result_int(count);
}
```

- `ctx.state<AnalyticsState>()` performs an $O(1)$ stack retrieval of the connection-bound state pointer.
- `SqliteExtState<AnalyticsState>::WriteGuard` provides RAII-scoped mutual exclusion.

---

### 3.5 Object-Oriented Aggregate (`geo_mean`)

Computes the geometric mean $\left(\prod_{i=1}^n x_i\right)^{1/n}$ across an arbitrary column of numbers:

```cpp
struct GeometricMeanAgg : public SqliteAggregateBase<double> {
    double product = 1.0;
    int count = 0;

    void step(SqliteUdfArgs args) override {
        if (args.size() > 0 && args[0].type() != SQLITE_NULL) {
            double val = args[0].as_double();
            if (val > 0.0) {
                product *= val;
                count++;
            }
        }
    }

    double finalize() override {
        if (count == 0) return 0.0;
        double root = product;
        for (int i = 0; i < 30; ++i) {
            double p = 1.0;
            for (int k = 0; k < count - 1; ++k) p *= root;
            if (p != 0.0) root = ((count - 1) * root + product / p) / count;
        }
        return root;
    }
};
```

- Inheriting from `SqliteAggregateBase<double>` automatically sets up SQLite's `xStep` and `xFinal` routines.
- SQLite allocates memory for `GeometricMeanAgg` directly in its aggregation context memory space.

---

### 3.6 Table-Valued Function (`fibonacci`)

A streaming Table-Valued Function that yields Fibonacci numbers on the fly:

```cpp
struct FibonacciIterator : public SqliteTvfIterator {
    static constexpr const char* schema() {
        return "CREATE TABLE x(idx INT, val INT, max_n hidden)";
    }

    int m_max = 10;
    int m_idx = 0;
    sqlite3_int64 m_curr = 0;
    sqlite3_int64 m_next = 1;

    void init(SqliteUdfArgs args) override {
        m_idx = 1;
        m_curr = 1;
        m_next = 1;
        m_max = (args.size() > 0 && args[0].type() != SQLITE_NULL) ? static_cast<int>(args[0].as_int64()) : 10;
    }

    void next() override {
        m_idx++;
        sqlite3_int64 tmp = m_curr + m_next;
        m_curr = m_next;
        m_next = tmp;
    }

    bool eof() const override { return m_idx > m_max; }

    void column(SqliteContext ctx, int col_idx) override {
        if (col_idx == 0) ctx.result_int(m_idx);
        else if (col_idx == 1) ctx.result_int64(m_curr);
    }

    sqlite3_int64 rowid() const override { return m_idx; }
};
```

- SQL callers invoke: `SELECT idx, val FROM fibonacci(8);`
- `max_n hidden` defines a hidden column mapped to SQL input arguments.

---

### 3.7 Unified Entrypoints & Facade Registration

Using the `SqliteExt` facade, all components are registered in a single clean function:

```cpp
static int register_all_components(SqliteDatabaseView db) {
    // 1. Initialize shared connection state
    SqliteExt::init_state<AnalyticsState>(db, [](AnalyticsState* s) {
        s->total_queries = 0;
        s->running_sum = 0.0;
        const char* tag = "EXAMPLE_SESSION";
        memcpy(s->session_tag, tag, strlen(tag) + 1);
    });

    // 2. Register Scalar UDFs
    SqliteExt::define_scalar(db, "math_hypot", 2, math_hypot);
    SqliteExt::define_scalar(db, "text_repeat", 2, text_repeat);
    SqliteExt::define_scalar_with_state<AnalyticsState, analytics_ping>(db, "analytics_ping", 0);

    // 3. Register Aggregate
    SqliteExt::define_aggregate<GeometricMeanAgg>(db, "geo_mean", 1);

    // 4. Register TVF
    SqliteExt::define_tvf<FibonacciIterator>(db, "fibonacci");

    return SQLITE_OK;
}

// Named Entrypoint: sqlite3_example_init
SQLITE_EXTENSION_ENTRYPOINT(example, db) {
    return register_all_components(db);
}

// Default Entrypoint: sqlite3_extension_init
SQLITE_DEFAULT_EXTENSION_ENTRYPOINT(db) {
    return register_all_components(db);
}
```

---

## 4. Compilation Architecture & Build Commands

### Compiler Flags Rationale:
- `-shared -fPIC`: Emits position-independent shared library binaries.
- `-std=c++11`: Modern C++ template metaprogramming.
- `-nostdlib++`: Drops `libstdc++`/`libc++` runtime dependency, producing lean binaries under 25KB.
- `-fno-exceptions -fno-rtti`: Guarantees deterministic execution without exception overhead or RTTI tables.
- `-I../include`: Direct access to `sqlite-ext-core` headers.

### Linux / macOS
```bash
mkdir -p build
g++ -shared -fPIC -O2 -std=c++11 -Wall -Wextra \
    -fno-exceptions -fno-rtti -nostdlib++ \
    -I../include -o build/libexample.so example.cpp
```

### Windows (MSYS2 / MinGW GCC)
```bash
mkdir -p build
g++ -shared -fPIC -O2 -std=c++11 -Wall -Wextra \
    -fno-exceptions -fno-rtti -nostdlib++ \
    -I../include -o build/libexample.dll example.cpp
```

### Windows (MSVC `cl.exe`)
```cmd
mkdir build
cl /LD /O2 /std:c++14 /GR- /EHsc- /W4 /I..\include example.cpp /link /OUT:build\libexample.dll
```

---

## 5. Running the Interactive Demo

The automated [`Makefile`](Makefile) compiles and runs the extension through SQLite CLI:

```bash
cd example-cpp
make run
```
*Or from repository root:*
```bash
make example-cpp
```

---

## 6. Expected Terminal Output & Verification

Executing `example.sql` against SQLite in-memory engine produces the following formatted results:

```
╭───────────┬────────────┬────────────╮
│ hypot_3_4 │ hypot_5_12 │ hypot_zero │
╞═══════════╪════════════╪════════════╡
│       5.0 │       13.0 │        0.0 │
╰───────────┴────────────┴────────────╯
╭────────────────╮
│ pythagorean_17 │
╞════════════════╡
│           17.0 │
╰────────────────╯
╭────────────────────╮
│   repeated_text    │
╞════════════════════╡
│ SQLiteSQLiteSQLite │
╰────────────────────╯
╭───────────────╮
│ query_count_1 │
╞═══════════════╡
│             1 │
╰───────────────╯
╭───────────────╮
│ query_count_2 │
╞═══════════════╡
│             2 │
╰───────────────╯
╭───────────────╮
│ query_count_3 │
╞═══════════════╡
│             3 │
╰───────────────╯
╭────────────┬────────────────╮
│ item_count │ geometric_mean │
╞════════════╪════════════════╡
│          3 │            8.0 │
╰────────────┴────────────────╯
╭────────────┬────────────────────╮
│ item_count │   geometric_mean   │
╞════════════╪════════════════════╡
│          5 │ 41538.374868278632 │
╰────────────┴────────────────────╯
╭──────┬──────────────────╮
│ term │ fibonacci_number │
╞══════╪══════════════════╡
│    1 │                1 │
│    2 │                1 │
│    3 │                2 │
│    4 │                3 │
│    5 │                5 │
│    6 │                8 │
│    7 │               13 │
│    8 │               21 │
╰──────┴──────────────────╯
╭─────────────┬───────────────────╮
│ total_terms │ max_fibonacci_val │
╞═════════════╪═══════════════════╡
│          12 │               144 │
╰─────────────┴───────────────────╯
╭───────────────┬──────────╮
│ even_term_idx │ even_val │
╞═══════════════╪══════════╡
│             3 │        2 │
│             6 │        8 │
│             9 │       34 │
╰───────────────┴──────────╯
```

---

## 7. Multi-Language Integration Guides

### 7.1 Python

```python
import sqlite3

# 1. Connect to SQLite
conn = sqlite3.connect(":memory:")
conn.enable_load_extension(True)

# 2. Load the extension (.dll on Windows, .so on Linux, .dylib on macOS)
conn.load_extension("./build/libexample")

# 3. Test Scalar UDF
cur = conn.cursor()
cur.execute("SELECT math_hypot(3.0, 4.0)")
print("Hypotenuse:", cur.fetchone()[0])  # 5.0

# 4. Test Aggregate
cur.execute("CREATE TABLE nums(v REAL);")
cur.executemany("INSERT INTO nums VALUES (?);", [(2.0,), (8.0,), (32.0,)])
cur.execute("SELECT geo_mean(v) FROM nums;")
print("Geometric Mean:", cur.fetchone()[0])  # 8.0

# 5. Test TVF
cur.execute("SELECT term, val FROM fibonacci(5);")
for term, val in cur.fetchall():
    print(f"Fib({term}) = {val}")
```

---

### 7.2 Node.js (`better-sqlite3`)

```javascript
const Database = require('better-sqlite3');
const db = new Database(':memory:');

// Load extension
db.loadExtension('./build/libexample');

// Query Scalar
const hypot = db.prepare('SELECT math_hypot(3.0, 4.0) AS res').get();
console.log('Hypot:', hypot.res); // 5.0

// Query TVF
const rows = db.prepare('SELECT idx, val FROM fibonacci(6)').all();
console.table(rows);
```

---

### 7.3 C++ Host Application

```cpp
#define SQLITE_CORE
#include <stdio.h>
#include <assert.h>
#include "sqlite3_db.hpp"
#include "sqlite3_statement.hpp"

int main() {
    SqliteDatabaseOwned db(":memory:");
    db.enable_load_extension(true);

    // Load extension dynamically
    int rc = db.load_extension("./build/libexample.dll", "sqlite3_example_init", nullptr);
    assert(rc == SQLITE_OK);

    // Query scalar
    SqliteStatement stmt = db.prepare("SELECT math_hypot(5.0, 12.0);");
    if (stmt.next()) {
        printf("math_hypot(5, 12) = %f\n", stmt.column_double(0)); // 13.0
    }
    return 0;
}
```

---

## 8. Critical Invariants & Best Practices

1. **Never Define `SQLITE_CORE` in Extensions**:
   - `SQLITE_CORE` instructs the preprocessor that SQLite symbols are linked statically. Loadable extensions must resolve routines dynamically via `sqlite3_api`.
2. **Never Link Against `-lsqlite3` when Compiling Extensions**:
   - SQLite dynamically injects the dispatch table pointer at load time via `pApi`.
3. **Contiguous 1-Based `argvIndex` Sequencing**:
   - When developing custom Table-Valued Functions (TVFs), hidden argument constraint mappings must always form dense 1-based index sequences (`1, 2, ..., N`).
4. **Exception & Thread Safety**:
   - Always use `ctx.result_*` methods and `SqliteStringOwned` for string return buffers.
   - Use `ReadGuard` / `WriteGuard` around `SqliteExtState` mutations.
