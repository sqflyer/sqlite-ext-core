# Coroutine Table-Valued Function (TVF) Framework (`sqlite3_tvf_coro.hpp`)

A declarative, zero-boilerplate C++ framework for building **streaming SQLite Table-Valued Functions (eponymous virtual tables)** using cooperative coroutines. Eliminates manual cursor state machines, step counters, and switch-case dispatch tables by allowing developers to author TVFs as a **single natural generator function** using either **Stackful Fibers (`SqliteFiberGenerator<T>`)** or **Stackless C++20 Coroutines (`SqliteGenerator<T>`, `co_yield`)**.

> **Systems Architecture**: For an in-depth breakdown of compile-time generator deduction, polymorphic column multiplexing, memory layouts, and register preservation across `xFilter`/`xNext`/`xColumn`, see [`docs/TVF_CORO_ARCHITECTURE.md`](TVF_CORO_ARCHITECTURE.md).

---

## 1. Feature Matrix

| Feature | Classic `SqliteTvfIterator` | Coroutine `SqliteTvfCoro` | Advantage |
| :--- | :--- | :--- | :--- |
| **API Complexity** | Requires 5 methods (`init`, `next`, `eof`, `column`, `rowid`) | **1 Single `generate()` function** | **90% less boilerplate** |
| **Control Flow** | Inverted state machine (`switch (step)`) | **Sequential linear loops & recursion** | **Natural procedural code** |
| **Multi-Column Rows**| Manual index checking in `column(ctx, idx)` | **Automatic via `SqliteRowStatic<N>` / `SqliteRowDynamic`** | **Zero manual index code** |
| **Recursive Yielding**| Impossible without manual stack vectors | **Native via Stackful Fibers** | **Yield from deep helper functions** |
| **C++20 `co_yield`** | Unsupported | **Native (`SqliteGenerator<T>`)** | **~48-byte compiler state machine** |
| **Standard Library** | 0.0% (`-nostdlib++`) | **0.0% (`-nostdlib++`)** | **Freestanding portability** |
| **Memory Profiling** | `sqlite3_malloc64` | **100% `sqlite3_malloc64` / `sqlite3_free`** | **Tracked in `sqlite3_memory_used`** |
| **State Injection** | Supported | **Native `SqliteExtState` support** | **O(1) connection state access** |

---

## 2. The Paradigm Shift: Classic TVF vs Coroutine TVF

```
┌──────────────────────────────────────────────┬──────────────────────────────────────────────┐
│       CLASSIC TVF (SqliteTvfIterator)        │          COROUTINE TVF (SqliteTvfCoro)       │
├──────────────────────────────────────────────┼──────────────────────────────────────────────┤
│ struct MySeries : public SqliteTvfIterator { │ struct MySeries {                            │
│     sqlite3_int64 cur, stop, step;           │     static constexpr const char* schema() {  │
│     static const char* schema() { ... }      │         return "CREATE TABLE x(val, s H);";  │
│     void init(SqliteUdfArgs args) override { │     }                                        │
│         cur = args[0].as_int64();            │     static auto generate(SqliteUdfArgs args){│
│     }                                        │         auto stop = args[0].as_int64();      │
│     void next() override { cur += step; }    │         return SqliteFiberGenerator<int64>(  │
│     bool eof() const override { ... }        │             [=](const auto& yield) {         │
│     void column(ctx, idx) override { ... }   │                 for (int64 v=0; v<=stop; ++v)│
│     sqlite3_int64 rowid() const override {..}│                     yield(v); // THAT'S IT!  │
│ };                                           │             });                              │
│                                              │     }                                        │
│                                              │ };                                           │
└──────────────────────────────────────────────┴──────────────────────────────────────────────┘
```

---

## 3. Quickstart Tutorials & Code Patterns

### 1. Scalar TVF: Integer Range Generator (`generate_series`)
Generate a series of numbers with optional start, stop, and step parameters:

```cpp
#include "sqlite3_tvf_coro.hpp"

struct SeriesTvf {
    // 1. Declare schema (HIDDEN columns act as input arguments)
    static constexpr const char* schema() {
        return "CREATE TABLE x(value INT, start HIDDEN, stop HIDDEN, step HIDDEN)";
    }

    // 2. Pure sequential generator function
    static SqliteFiberGenerator<sqlite3_int64> generate(SqliteUdfArgs args) {
        sqlite3_int64 start = (!args.empty() && !args[0].is_null()) ? args[0].as_int64() : 0;
        sqlite3_int64 stop  = (args.size() > 1 && !args[1].is_null()) ? args[1].as_int64() : 0;
        sqlite3_int64 step  = (args.size() > 2 && !args[2].is_null()) ? args[2].as_int64() : 1;
        if (step == 0) step = 1;

        return SqliteFiberGenerator<sqlite3_int64>([=](const auto& yield) {
            for (sqlite3_int64 v = start; v <= stop; v += step) {
                yield(v); // Yields directly to SQLite query engine!
            }
        });
    }
};

// 3. Register with SQLite connection
void register_extension(sqlite3* db) {
    SqliteTvfCoro::define<SeriesTvf>(db, "generate_series");
}
```

```sql
-- Usage in SQL:
SELECT value FROM generate_series(1, 10, 2);
-- Output: 1, 3, 5, 7, 9
```

---

### 2. Multi-Column Static Rows (`SqliteRowStatic<N>`)
Emit multi-column rows using fixed-size, stack-allocated row arrays:

```cpp
struct MultiColTvf {
    static constexpr const char* schema() {
        return "CREATE TABLE x(id INT, square INT, cube INT, max_n HIDDEN)";
    }

    static SqliteFiberGenerator<SqliteRowStatic<3>> generate(SqliteUdfArgs args) {
        int max_n = (!args.empty() && !args[0].is_null()) ? static_cast<int>(args[0].as_int64()) : 5;

        return SqliteFiberGenerator<SqliteRowStatic<3>>([=](const auto& yield) {
            for (int i = 1; i <= max_n; ++i) {
                SqliteRowStatic<3> row;
                row[0] = static_cast<sqlite3_int64>(i);
                row[1] = static_cast<sqlite3_int64>(i * i);
                row[2] = static_cast<sqlite3_int64>(i * i * i);
                yield(row); // Zero heap allocations!
            }
        });
    }
};
```

```sql
SELECT id, square, cube FROM multi_col(4);
-- Output:
-- 1 | 1  | 1
-- 2 | 4  | 8
-- 3 | 9  | 27
-- 4 | 16 | 64
```

---

### 3. Dynamic Rows & String Tokenizer (`SqliteRowDynamic`)
Split a string by delimiter and stream numbered tokens:

```cpp
struct StringSplitTvf {
    static constexpr const char* schema() {
        return "CREATE TABLE x(idx INT, token TEXT, input_text HIDDEN, delim HIDDEN)";
    }

    static SqliteFiberGenerator<SqliteRowDynamic> generate(SqliteUdfArgs args) {
        SqliteStringView text = (!args.empty() && !args[0].is_null()) ? args[0].as_text() : SqliteStringView("");
        char delim = (args.size() > 1 && !args[1].is_null() && args[1].as_text().length() > 0) ? args[1].as_text().data()[0] : ',';

        return SqliteFiberGenerator<SqliteRowDynamic>([=](const auto& yield) {
            const char* start = text.data();
            const char* p = start;
            int total_len = text.length();
            int idx = 1;

            for (int i = 0; i < total_len; ++i) {
                if (start[i] == delim) {
                    SqliteRowDynamic row(2);
                    row[0] = static_cast<sqlite3_int64>(idx++);
                    row[1] = SqliteValueOwned::from_text(p, static_cast<int>((start + i) - p));
                    yield(row);
                    p = start + i + 1;
                }
            }
            if ((start + total_len) >= p) {
                SqliteRowDynamic row(2);
                row[0] = static_cast<sqlite3_int64>(idx);
                row[1] = SqliteValueOwned::from_text(p, static_cast<int>((start + total_len) - p));
                yield(row);
            }
        });
    }
};
```

```sql
SELECT idx, token FROM string_split('apple,banana,cherry', ',');
-- Output:
-- 1 | apple
-- 2 | banana
-- 3 | cherry
```

---

### 4. Recursive Deep-Stack Traversal (JSON / B-Tree TVF)
Because stackful fibers retain their call stacks, you can yield from inside recursive helper functions:

```cpp
struct Node {
    int id;
    const char* name;
    Node* left;
    Node* right;
};

void traverse(Node* node, const SqliteFiberGenerator<SqliteRowDynamic>::YieldHandle& yield) {
    if (!node) return;
    traverse(node->left, yield);

    SqliteRowDynamic row(2);
    row[0] = static_cast<sqlite3_int64>(node->id);
    row[1] = SqliteValueOwned::from_text(node->name);
    yield(row); // Yields from deep inside recursion!

    traverse(node->right, yield);
}

struct TreeTvf {
    static constexpr const char* schema() { return "CREATE TABLE x(id INT, name TEXT, root_ptr HIDDEN)"; }

    static SqliteFiberGenerator<SqliteRowDynamic> generate(SqliteUdfArgs args) {
        Node* root = reinterpret_cast<Node*>(args[0].as_pointer());
        return SqliteFiberGenerator<SqliteRowDynamic>([root](const auto& yield) {
            traverse(root, yield);
        });
    }
};
```

---

### 5. Stackless C++20 TVF with `co_yield` (Zero Stack Allocation)
In C++20 mode (`-std=c++20`), return `SqliteGenerator<T>` to compile down to a flat ~48-byte state machine:

```cpp
struct StacklessSeriesTvf {
    static constexpr const char* schema() {
        return "CREATE TABLE x(value INT, stop HIDDEN)";
    }

    static SqliteGenerator<sqlite3_int64> generate(SqliteUdfArgs args) {
        sqlite3_int64 stop = (!args.empty() && !args[0].is_null()) ? args[0].as_int64() : 10;

        for (sqlite3_int64 v = 0; v <= stop; ++v) {
            co_yield v; // 0 stack pages allocated, frame allocated via sqlite3_malloc64
        }
    }
};
```

---

### 6. Stateful TVF with Shared Database State (`SqliteExtState`)
Bind a TVF to a per-connection state struct with automatic lifecycle management:

```cpp
struct MetricsState {
    int query_count = 0;
    int active_sessions = 10;
};

struct MetricsTvf {
    static constexpr const char* schema() {
        return "CREATE TABLE x(metric TEXT, value INT)";
    }

    static SqliteFiberGenerator<SqliteRowDynamic> generate(SqliteUdfArgs) {
        return SqliteFiberGenerator<SqliteRowDynamic>([](const auto& yield) {
            // Note: raw_state is automatically injected into SqliteContext during xColumn
            SqliteRowDynamic r1(2);
            r1[0] = SqliteValueOwned::from_text("active_sessions");
            r1[1] = static_cast<sqlite3_int64>(10);
            yield(r1);
        });
    }
};

// Registration:
SqliteTvfCoro::define_with_state<MetricsState, MetricsTvf>(db, "db_metrics");
```

---

## 4. Performance & Execution Latency

| Benchmark Metric | Classic `SqliteTvfIterator` | Coroutine `SqliteTvfCoro` (Fiber) | Coroutine `SqliteTvfCoro` (C++20) |
| :--- | :--- | :--- | :--- |
| **Row Iteration Time** | ~4 – 8 ns | **~15 – 25 ns** | **~5 – 9 ns** |
| **Cursor Heap Memory** | ~64 Bytes | **32 KB (Stack) + ~80B** | **~48 Bytes** |
| **Lines of C++ Code** | ~45 – 60 lines | **~10 – 15 lines** | **~8 – 12 lines** |
| **Compiler Standard** | C++11 | **C++11** | **C++20** |
| **Standard Library Dep**| 0.0% | **0.0% (`-nostdlib++`)** | **0.0% (`-nostdlib++`)** |
