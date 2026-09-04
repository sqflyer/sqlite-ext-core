# C++ Table-Valued Function (TVF) Framework (`sqlite3_tvf.hpp`)

A zero-boilerplate, zero-dependency C++ framework for building eponymous SQLite Table-Valued Functions (Virtual Tables). It completely abstracts away SQLite's complex Virtual Table C-API (`sqlite3_module`, `sqlite3_vtab`, `sqlite3_vtab_cursor`, `xBestIndex`), allowing developers to create high-performance streaming TVFs by simply implementing an intuitive C++ iterator class.

> **Architecture Reference**: For an in-depth breakdown of static module generation, `xBestIndex` constraint routing, and `-nostdlib++` memory safety, see [`docs/TVF_ARCHITECTURE.md`](TVF_ARCHITECTURE.md).

---

## 1. Features Matrix

| Feature | Description |
| :--- | :--- |
| **Zero C-API Boilerplate** | No manual `sqlite3_module` structs or verbose `xConnect`/`xBestIndex`/`xFilter` callbacks. |
| **Modern `SqliteContext`** | Produce typed column outputs directly via `ctx.result_int64()`, `ctx.result_text()`, or `val.result(ctx)`. |
| **Bounds-Safe `SqliteUdfArgs`** | Hidden schema columns are automatically routed into `init(SqliteUdfArgs args)` with out-of-bounds safety. |
| **Shared Stateful TVFs** | Stream rows directly from shared `SqliteExtState<T>` structs or mutate per-connection state during iteration with thread-safe RAII locking. |
| **Stateful Registration (`define_tvf_with_state`)** | Binds shared state via `sqlite3_create_module_v2` with automated `xDestroy` garbage collection. |
| **Query Planner Auto-Tuning** | Generically manages `xBestIndex` equality constraints and cost heuristics to optimize correlated subqueries and `JOIN` plans. |
| **Compile-Time Static Bridge** | `SqliteTvfModule<T>` generates static C-callback trampolines that cast SQLite pointers directly to `T*`. |
| **Zero Virtual Destructor Overhead** | Employs protected non-virtual base destructors with explicit downcasting in `xClose`, eliminating global `operator delete` runtime dependencies. |
| **Freestanding & `-nostdlib++`** | 100% header-only, zero dependencies on standard library runtime heaps (`<vector>`, `<memory>`, `<functional>`). |

---

## 2. Quickstart Tutorial: Building `generate_series`

To build a TVF, define an iterator struct publicly inheriting from `SqliteTvfIterator` and implement the 5 required lifecycle methods:

```cpp
#include "sqlite3_tvf.hpp"
#include "sqlite3_udf.hpp"

struct SeriesIterator : public SqliteTvfIterator {
    sqlite3_int64 m_current = 0;
    sqlite3_int64 m_stop = 0;
    sqlite3_int64 m_step = 1;

    // 1. Declare Schema: Columns marked HIDDEN serve as input arguments!
    static constexpr const char* schema() {
        return "CREATE TABLE x(value, start HIDDEN, stop HIDDEN, step HIDDEN)";
    }

    // 2. Initialize the iterator when a query executes
    void init(SqliteUdfArgs args) override {
        m_current = args.size() > 0 && args[0].type() != SQLITE_NULL ? args[0].as_int64() : 0;
        m_stop    = args.size() > 1 && args[1].type() != SQLITE_NULL ? args[1].as_int64() : 0;
        m_step    = args.size() > 2 && args[2].type() != SQLITE_NULL ? args[2].as_int64() : 1;
        if (m_step == 0) m_step = 1;
    }

    // 3. Advance to the next row
    void next() override {
        m_current += m_step;
    }

    // 4. End-of-Stream check
    bool eof() const override {
        return (m_step > 0) ? (m_current > m_stop) : (m_current < m_stop);
    }

    // 5. Output current row column value using SqliteContext
    void column(SqliteContext ctx, int col_idx) override {
        if (col_idx == 0) { // Column 0 corresponds to 'value'
            ctx.result_int64(m_current);
        }
    }

    // 6. Provide a unique 64-bit row ID
    sqlite3_int64 rowid() const override {
        return m_current;
    }
};
```

> [!TIP]
> **Error Propagation**: If a column calculation or data fetching operation fails, use `SqliteResult<T>` or `SqliteStatus` with `res.set_sqlite_err(ctx.get())` to set the appropriate SQLite engine error code without throwing exceptions.

---

## 3. Registering and Executing the TVF

### Registration
Register the TVF on the database connection in a single line using `SqliteTvf::define<T>`:

```cpp
void register_tvfs(SqliteDatabaseView db) {
    SqliteTvf::define<SeriesIterator>(db, "generate_series");
    // Or via umbrella: SqliteExt::define_tvf<SeriesIterator>(db, "generate_series");
}
```

### SQL Usage:
```sql
-- Direct TVF call
SELECT value FROM generate_series(1, 10, 2);
-- Output: 1, 3, 5, 7, 9

-- Correlated Subquery / Lateral Join
CREATE TABLE ranges(id INTEGER PRIMARY KEY, min_val INT, max_val INT);
INSERT INTO ranges VALUES (1, 5, 8), (2, 20, 22);

SELECT r.id, s.value 
FROM ranges r 
JOIN generate_series(r.min_val, r.max_val) s;
```

---

## 4. Multi-Column TVF Example (`str_split`)

TVFs can return rich tabular structures with multiple columns (strings, numbers, timestamps):

```cpp
struct SplitIterator : public SqliteTvfIterator {
    const char* text_ptr = nullptr;
    int text_len = 0;
    int current_pos = 0;
    int next_delimiter = -1;
    int item_index = 0;

    static constexpr const char* schema() {
        return "CREATE TABLE x(token_index, token, input_text HIDDEN)";
    }

    void init(SqliteUdfArgs args) override {
        if (args.size() > 0 && args[0].type() == SQLITE_TEXT) {
            SqliteStringView input = args[0].as_text();
            text_ptr = input.data();
            text_len = input.length();
        } else {
            text_ptr = nullptr;
            text_len = 0;
        }
        current_pos = 0;
        item_index = 0;
        find_next();
    }

    void find_next() {
        if (current_pos >= text_len) {
            next_delimiter = -1;
            return;
        }
        next_delimiter = current_pos;
        while (next_delimiter < text_len && text_ptr[next_delimiter] != ',') {
            next_delimiter++;
        }
    }

    void next() override {
        current_pos = next_delimiter + 1;
        item_index++;
        find_next();
    }

    bool eof() const override {
        return current_pos > text_len || text_ptr == nullptr;
    }

    void column(SqliteContext ctx, int col_idx) override {
        if (col_idx == 0) {
            ctx.result_int(item_index);
        } else if (col_idx == 1) {
            int len = next_delimiter - current_pos;
            SqliteStringView token(text_ptr + current_pos, len);
            token.result(ctx);
        }
    }

    sqlite3_int64 rowid() const override {
        return item_index;
    }
};

// Register:
SqliteTvf::define<SplitIterator>(db, "str_split");
```

```sql
SELECT token_index, token FROM str_split('apple,banana,cherry');
-- Row 1: 0 | apple
-- Row 2: 1 | banana
-- Row 3: 2 | cherry
```

---

## 5. Stateful TVFs: Streaming from Shared `SqliteExtState<T>`

When a Table-Valued Function needs to stream internal state, configuration metrics, or session counters maintained across other UDFs and aggregates on the database connection, use **`SqliteTvf::define_with_state`** (or **`SqliteExt::define_tvf_with_state`**).

### How State Injection Works
Unlike Scalar UDFs and Aggregates where SQLite automatically passes `pApp` to `sqlite3_user_data(ctx)`, SQLite Virtual Table `xColumn` callbacks receive an ephemeral context where `sqlite3_user_data(ctx)` is `NULL`.

The TVF framework solves this via **Direct Context Injection**:
1. When registered via `define_with_state<State, Iterator>`, `sqlite3_create_module_v2` receives `raw_state` as `pClientData`.
2. In `xConnect`, the module captures `pAux` directly onto the `VTab` holder (`pTab->raw_state = pAux`).
3. In `xColumn`, the module constructs `SqliteContext(ctx, pTab->raw_state)` on the stack.
4. Calling `ctx.state<State>()` accesses the injected pointer directly in **1 CPU instruction ($O(1)$)** without any hash table searches or database handle lookups.

### Step 1: Define Shared State Struct
```cpp
struct AppMetricsState {
    int total_queries;
    int cache_hits;
    int cache_misses;
};
```

### Step 2: Define the Stateful TVF Iterator
```cpp
struct MetricsIterator : public SqliteTvfIterator {
    int row_idx = 0;

    static constexpr const char* schema() {
        return "CREATE TABLE x(metric_name TEXT, metric_value INT)";
    }

    void init(SqliteUdfArgs args) override {
        (void)args;
        row_idx = 0;
    }

    void next() override { row_idx++; }
    bool eof() const override { return row_idx >= 3; }

    void column(SqliteContext ctx, int col_idx) override {
        // Direct O(1) state retrieval from the injected context:
        AppMetricsState* state = ctx.state<AppMetricsState>();
        if (!state) {
            ctx.result_null();
            return;
        }

        int queries = 0, hits = 0, misses = 0;
        {
            SqliteExtState<AppMetricsState>::ReadGuard lock(state);
            queries = lock->total_queries;
            hits = lock->cache_hits;
            misses = lock->cache_misses;
        }

        if (col_idx == 0) {
            const char* names[] = {"total_queries", "cache_hits", "cache_misses"};
            ctx.result_text(names[row_idx]);
        } else if (col_idx == 1) {
            int vals[] = {queries, hits, misses};
            ctx.result_int(vals[row_idx]);
        }
    }

    sqlite3_int64 rowid() const override { return row_idx; }
};
```

### Step 3: Register the Stateful TVF
```cpp
void setup_stateful_tvf(SqliteDatabaseView db) {
    // 1. Initialize per-database shared state
    SqliteExtState<AppMetricsState>::get_or_create(db.get(), [](AppMetricsState* s) {
        s->total_queries = 120;
        s->cache_hits = 95;
        s->cache_misses = 25;
    });

    // 2. Register TVF bound to shared state with automated xDestroy cleanup
    SqliteTvf::define_with_state<AppMetricsState, MetricsIterator>(db, "app_metrics");
    // Or via umbrella: SqliteExt::define_tvf_with_state<AppMetricsState, MetricsIterator>(db, "app_metrics");
}
```

### SQL Usage:
```sql
SELECT metric_name, metric_value FROM app_metrics();
-- Row 1: total_queries | 120
-- Row 2: cache_hits    | 95
-- Row 3: cache_misses  | 25
```

---

## 6. Query Planner Cost Auto-Tuning

The SQLite query optimizer calls `xBestIndex` to determine the execution cost of the TVF. 

By default, `SqliteTvfIterator::estimated_cost(int usable_args)` returns a cost **inversely proportional to the number of bound arguments**:
$$\text{Cost} = \frac{100000.0}{\text{usable\_args} + 1}$$

This golden heuristic forces SQLite to favor query plans that provide the maximum number of arguments to the TVF.

### Custom Cost Override
To provide a custom cost formula, define a `static double estimated_cost(int)` method in your iterator struct. C++ static name hiding will automatically dispatch to it with zero virtual overhead:

```cpp
struct FastIndexIterator : public SqliteTvfIterator {
    // Custom planner cost override
    static double estimated_cost(int usable_args) {
        return (usable_args >= 2) ? 1.0 : 10000.0;
    }
    // ...
};
```

---

## 7. Deep-Dive Architecture Documentation

For complete internal design details, static trampoline generation, and memory models:
- **[`docs/TVF_ARCHITECTURE.md`](TVF_ARCHITECTURE.md)**: Deep dive into `SqliteTvfModule<T>` C-module scaffolding, `xBestIndex` constraint mapping, and `-nostdlib++` memory safety without virtual destructors.
