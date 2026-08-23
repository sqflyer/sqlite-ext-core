# C++ User-Defined Function (UDF) Framework (`sqlite3_udf.hpp`)

`sqlite3_udf.hpp` provides a unified, zero-overhead modern C++11 framework for defining and registering SQLite User-Defined Functions (UDFs). It supports **Stateless Scalar Functions**, **Thread-Safe Stateful Functions** (sharing state across multiple UDFs via `SqliteExtState<T>`), **Object-Oriented Aggregates**, and **Table-Valued Functions (TVFs)** with bounds-safe argument checking and zero runtime heap allocation.

---

## 1. Features Matrix

| Feature | Description |
| :--- | :--- |
| **Zero Boilerplate Registration** | Register scalar functions in a single line with `SqliteUdf::define(db, "name", num_args, func)`. |
| **Modern `SqliteContext`** | Pass and manipulate execution contexts using zero-overhead `SqliteContext` (`ctx.result_int64(...)`, `ctx.result_error(...)`, `ctx.state<T>()`). |
| **Bounds-Safe `SqliteUdfArgs`** | Out-of-bounds indexing (e.g. `args[-1]`, `args[999]`) safely produces `SQLITE_NULL` views rather than segmentation faults. |
| **Shared Stateful UDFs** | Multiple UDFs can share and mutate the exact same `SqliteExtState<T>` struct instance with thread-safe RAII locking. |
| **Compile-Time Template Proxies** | `SqliteUdf::define_with_state<State, func>(db, "name", num_args)` binds raw state directly to `pApp` with zero heap allocation. |
| **Stateless C++11 Lambdas** | Pass inline stateless lambdas directly without writing separate forward declarations or helper functions. |
| **Aggregates & TVFs** | Native registration for C++ Aggregate classes (`define_aggregate<T>`) and Table-Valued Functions (`define_tvf<T>`). |
| **Freestanding & `-nostdlib++`** | 100% header-only, zero dependencies on standard library runtime heaps (`<functional>`, `<vector>`, `<memory>`). |

---

## 2. Quickstart Tutorial

### 2.1 Basic Math Scalar Function
```cpp
#include "sqlite3_udf.hpp"

// Define a function that adds two numbers using SqliteContext
static void udf_add(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() != 2) {
        ctx.result_error("udf_add requires exactly 2 arguments");
        return;
    }
    sqlite3_int64 a = args[0].as_int64();
    sqlite3_int64 b = args[1].as_int64();
    ctx.result_int64(a + b);
}

void register_functions(SqliteDatabaseView db) {
    SqliteUdf::define(db, "add_numbers", 2, udf_add);
}
```

---

### 2.2 Inline Stateless C++11 Lambdas
You can register stateless lambdas directly:
```cpp
SqliteUdf::define(db, "square", 1, [](SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() != 1) return;
    sqlite3_int64 val = args[0].as_int64();
    ctx.result_int64(val * val);
});
```

---

### 2.3 Dynamic Variadic Functions
Pass `-1` as `num_args` to accept variable arguments:
```cpp
static void udf_sum_all(SqliteContext ctx, SqliteUdfArgs args) {
    double total = 0.0;
    for (int i = 0; i < args.size(); i++) {
        if (args[i].type() == SQLITE_INTEGER) {
            total += args[i].as_int64();
        } else if (args[i].type() == SQLITE_FLOAT) {
            total += args[i].as_double();
        } else if (args[i].type() == SQLITE_NULL) {
            continue;
        } else {
            ctx.result_error("sum_all only accepts numeric parameters");
            return;
        }
    }
    ctx.result_double(total);
}

SqliteUdf::define(db, "sum_all", -1, udf_sum_all);
```

---

### 2.4 String & Blob Builders
Combine `SqliteUdfArgs` with `SqliteStringOwned` or `SqliteBlobOwned` for memory-safe result generation:
```cpp
static void udf_repeat_text(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() != 2) return;

    SqliteStringView str = args[0].as_text();
    int count = args[1].as_int64();

    SqliteStringOwned result(ctx.get());
    for (int i = 0; i < count; i++) {
        result.append(str.data(), str.length());
    }
    
    // Result returned safely transferring memory ownership
    result.result(ctx);
}

SqliteUdf::define(db, "repeat_text", 2, udf_repeat_text);
```

---

## 3. Stateful UDFs: Multiple Functions Sharing the Same State

When multiple functions need to read or mutate shared runtime state (e.g. counters, accumulators, caches, or connection contexts), use **`SqliteUdf::define_with_state`** and **`SqliteExtState<T>`**.

### Step 1: Define Shared State Struct
```cpp
struct AppState {
    int counter;
    int accumulator;
    char last_tag[64];
};
```

### Step 2: Implement Functions Sharing the State
```cpp
// Function 1: Increment counter (Write lock)
static void state_inc(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    AppState* state = SqliteExtState<AppState>::from_context(ctx);
    if (!state) return;

    int current = 0;
    {
        SqliteExtState<AppState>::WriteGuard lock(state);
        lock->counter++;
        current = lock->counter;
    }
    ctx.result_int(current);
}

// Function 2: Add delta to accumulator (Write lock)
static void state_accumulate(SqliteContext ctx, SqliteUdfArgs args) {
    AppState* state = ctx.state<AppState>();
    if (!state) return;

    int total = 0;
    {
        SqliteExtState<AppState>::WriteGuard lock(state);
        lock->accumulator += args[0].as_int64();
        total = lock->accumulator;
    }
    ctx.result_int(total);
}

// Function 3: Format and return all stats (Read lock)
static void state_get_stats(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    AppState* state = ctx.state<AppState>();
    if (!state) return;

    int c = 0, a = 0;
    char tag[64];
    {
        SqliteExtState<AppState>::ReadGuard lock(state);
        c = lock->counter;
        a = lock->accumulator;
        memcpy(tag, lock->last_tag, sizeof(tag));
    }

    SqliteStringOwned out(ctx.get());
    out.appendall("counter=");
    char num[32];
    snprintf(num, sizeof(num), "%d", c);
    out.appendall(num);
    out.appendall(" acc=");
    snprintf(num, sizeof(num), "%d", a);
    out.appendall(num);
    out.result(ctx);
}
```

### Step 3: Register Functions on the Database
```cpp
void setup_stateful_functions(SqliteDatabaseView db) {
    // 1. Initialize custom starting state for this connection
    SqliteExtState<AppState>::get_or_create(db.get(), [](AppState* s) {
        s->counter = 100;
        s->accumulator = 0;
        s->last_tag[0] = '\0';
    });

    // 2. Register multiple functions sharing the same state
    SqliteUdf::define_with_state<AppState, state_inc>(db, "state_inc", 0);
    SqliteUdf::define_with_state<AppState, state_accumulate>(db, "state_accumulate", 1);
    SqliteUdf::define_with_state<AppState, state_get_stats>(db, "state_get_stats", 0);
}
```

### SQL Usage:
```sql
SELECT state_inc();         -- Returns 101
SELECT state_inc();         -- Returns 102
SELECT state_accumulate(50); -- Returns 50
SELECT state_get_stats();   -- Returns 'counter=102 acc=50'
```

---

## 4. Table-Valued Functions (TVF) & Aggregates

### Table-Valued Functions (`define_tvf`)
```cpp
struct RangeIterator : public SqliteTvfIterator {
    static constexpr const char* schema() {
        return "CREATE TABLE x(value, stop hidden)";
    }
    sqlite3_int64 curr = 0, stop = 0;
    void init(SqliteUdfArgs args) override {
        curr = 0;
        stop = args.size() > 0 ? args[0].as_int64() : 0;
    }
    void next() override { curr++; }
    bool eof() const override { return curr > stop; }
    void column(SqliteContext ctx, int col) override { if (col == 0) ctx.result_int64(curr); }
    sqlite3_int64 rowid() const override { return curr; }
};

SqliteUdf::define_tvf<RangeIterator>(db, "generate_range");
```

### Object-Oriented Aggregates (`define_aggregate`)
```cpp
struct StdDevAgg : public SqliteAggregateBase {
    double sum = 0.0;
    double sum_sq = 0.0;
    int count = 0;

    void step(SqliteContext ctx, SqliteUdfArgs args) {
        (void)ctx;
        double val = args[0].as_double();
        sum += val;
        sum_sq += val * val;
        count++;
    }

    void finalize(SqliteContext ctx) {
        if (count == 0) { ctx.result_null(); return; }
        double mean = sum / count;
        double variance = (sum_sq / count) - (mean * mean);
        ctx.result_double(sqrt(variance));
    }
};

SqliteUdf::define_aggregate<StdDevAgg>(db, "std_dev", 1);
```

---

## 5. API Reference Summary

### Stateless Registration:
- `SqliteUdf::define(db, name, num_args, func, deterministic = true)`
  - `func`: `void(*)(SqliteContext, SqliteUdfArgs)`
  - `func`: `void(*)(SqliteContext&, SqliteUdfArgs)`
  - `func`: `void(*)(sqlite3_context*, SqliteUdfArgs)`

### Stateful Registration:
- `SqliteUdf::define_with_state<State, Func>(db, name, num_args, deterministic = false)`
  - Direct compile-time proxy binding `raw_state` to `pApp` with automated `xDestroy` garbage collection.

### State Access Helpers:
- `SqliteExtState<State>::from_context(ctx)`
- `ctx.state<State>()`
- `SqliteExtState<State>::get_or_create(db, init_fn)`
