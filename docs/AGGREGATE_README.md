# C++ Aggregate Function Framework (`sqlite3_aggregate.hpp`)

A zero-boilerplate, zero-dependency, type-safe C++ framework for defining SQLite Aggregate Functions using clean, object-oriented structs. It eliminates raw C pointers, manual `sqlite3_aggregate_context` memory tracking, and C-style casts while remaining 100% freestanding and `-nostdlib++` compliant.

> **Architecture Reference**: For an in-depth breakdown of `sqlite3_aggregate_context` memory layouts, 4-tier tag-dispatch finalize SFINAE, and shared state lifecycles, see [`docs/AGGREGATE_ARCHITECTURE.md`](AGGREGATE_ARCHITECTURE.md).

---

## 1. Features Matrix

| Feature | Description |
| :--- | :--- |
| **Object-Oriented Aggregate Structs** | Define aggregation state as intuitive C++ structs with `step()` and `finalize()` methods. |
| **Single-Line Registration** | Register aggregates via `SqliteUdf::define_aggregate<T>` or `SqliteAggregate<T>::define`. |
| **Shared Stateful Aggregates** | Share per-connection state structs across Aggregates, Scalar UDFs, and TVFs using `define_aggregate_with_state<State, T>`. |
| **Automatic Return Type Dispatching** | Return primitives (`int`, `sqlite3_int64`, `double`, `bool`), `const char*`, or wrappers (`SqliteStringOwned`, `SqliteBlobOwned`, `SqliteValueOwned`) directly from `finalize()`. |
| **Modern `SqliteContext` Support** | Full support for `step(SqliteContext ctx, SqliteUdfArgs args)` and `finalize(SqliteContext ctx)` for direct context access, error reporting, and state retrieval. |
| **Bounds-Safe Parameter Access** | `step(SqliteUdfArgs args)` provides bounds-checked, zero-allocation `SqliteValueView` access. |
| **RAII Lifecycle & Destructor Cleanup** | Automatically constructs the struct in-place on the first row and deterministically triggers `~T()` when SQLite finishes aggregation. |
| **Safe Empty-Set Aggregation** | Handles empty table sets gracefully by finalizing a default-constructed instance without crashes or undefined behavior. |
| **Freestanding & `-nostdlib++` Ready** | Compiles cleanly with `-fno-exceptions -fno-rtti -nostdlib++`. |

---

## 2. Quickstart Examples

### 1. Basic Numeric Aggregate (Custom Average)
```cpp
#include "sqlite3_aggregate.hpp"
#include "sqlite3_udf.hpp"

struct MyAvg : public SqliteAggregateBase<double> {
    double total = 0.0;
    int count = 0;

    void step(SqliteUdfArgs args) override {
        if (args[0].type() != SQLITE_NULL) {
            total += args[0].as_double();
            count++;
        }
    }

    double finalize() override {
        return count > 0 ? (total / count) : 0.0;
    }
};

// Register aggregate function
void register_aggregates(SqliteDatabaseView db) {
    SqliteAggregate::define<MyAvg>(db, "my_avg", 1);
    // Or via umbrella: SqliteExt::define_aggregate<MyAvg>(db, "my_avg", 1);
}
```

```sql
SELECT my_avg(score) FROM students;
```

---

### 2. Dynamic String Concatenation (`SqliteStringOwned`)
```cpp
struct GroupConcat : public SqliteAggregateBase<SqliteStringOwned> {
    SqliteStringOwned str;
    bool first = true;

    void step(SqliteUdfArgs args) override {
        if (args[0].type() == SQLITE_TEXT) {
            if (!first) str.append(", ", 2);
            first = false;
            
            SqliteStringView text = args[0].as_text();
            str.append(text.data(), text.length());
        }
    }

    SqliteStringOwned finalize() override {
        return sqlite_move_ptr(str);
    }
};

SqliteAggregate::define<GroupConcat>(db, "group_concat_custom", 1);
```

```sql
SELECT department, group_concat_custom(employee_name) 
FROM employees 
GROUP BY department;
```

---

### 3. Binary Blob Accumulator (`SqliteBlobOwned`)
```cpp
struct BlobCollector : public SqliteAggregateBase<SqliteBlobOwned> {
    unsigned char buffer[256];
    int size = 0;

    void step(SqliteUdfArgs args) override {
        if (args[0].type() == SQLITE_BLOB) {
            SqliteBlobView blob = args[0].as_blob();
            if (size + blob.size() <= 256) {
                memcpy(buffer + size, blob.data(), blob.size());
                size += blob.size();
            }
        }
    }

    SqliteBlobOwned finalize() override {
        return SqliteBlobOwned(buffer, size);
    }
};

SqliteAggregate::define<BlobCollector>(db, "blob_accum", 1);
```

---

### 4. Multi-Argument & Variadic Aggregates (Weighted Average)
```cpp
struct WeightedAvg : public SqliteAggregateBase<double> {
    double weighted_sum = 0.0;
    double total_weight = 0.0;

    void step(SqliteUdfArgs args) override {
        if (args.size() >= 2 && args[0].type() != SQLITE_NULL && args[1].type() != SQLITE_NULL) {
            double val = args[0].as_double();
            double weight = args[1].as_double();
            weighted_sum += (val * weight);
            total_weight += weight;
        }
    }

    double finalize() override {
        return total_weight > 0.0 ? (weighted_sum / total_weight) : 0.0;
    }
};

// 2 arguments: value, weight
SqliteAggregate::define<WeightedAvg>(db, "weighted_avg", 2);
```

```sql
SELECT weighted_avg(price, quantity) FROM orders;
```

---

### 5. Context-Aware Aggregates with Typed Return (`finalize(SqliteContext)`)
```cpp
struct StrictPositiveSum : public SqliteAggregateBase<sqlite3_int64> {
    sqlite3_int64 sum = 0;

    void step(SqliteContext ctx, SqliteUdfArgs args) override {
        sqlite3_int64 val = args[0].as_int64();
        if (val < 0) {
            ctx.result_error("Negative numbers are disallowed");
            return;
        }
        sum += val;
    }

    sqlite3_int64 finalize(SqliteContext ctx) override {
        // Can set custom headers/auxdata, or simply return typed sum:
        return sum;
    }
};

SqliteAggregate::define<StrictPositiveSum>(db, "strict_sum", 1);
```

---

## 3. Stateful Aggregates: Sharing State Across UDFs & TVFs

When an aggregate function needs to read or mutate shared per-connection state (such as configurations, audit counters, or runtime filters) alongside other UDFs and TVFs, use **`SqliteUdf::define_aggregate_with_state`**.

### Step 1: Define Shared State Struct
```cpp
struct MetricSharedState {
    int total_aggregations;
    char prefix_tag[32];
};
```

### Step 2: Define the Stateful Aggregate
```cpp
struct TaggedConcat : public SqliteAggregateBase<SqliteStringOwned> {
    SqliteStringOwned out;
    bool first = true;

    void step(SqliteContext ctx, SqliteUdfArgs args) override {
        if (args[0].type() == SQLITE_TEXT) {
            if (!first) out.append(", ", 2);
            first = false;
            SqliteStringView val = args[0].as_text();
            out.append(val.data(), val.length());
        }
    }

    SqliteStringOwned finalize(SqliteContext ctx) override {
        MetricSharedState* state = ctx.state<MetricSharedState>();
        if (state) {
            SqliteExtState<MetricSharedState>::WriteGuard lock(state);
            lock->total_aggregations++;
            
            // Prepend the shared tag prefix
            SqliteStringOwned prefixed(ctx.get());
            prefixed.appendall(lock->prefix_tag);
            prefixed.appendall(":");
            prefixed.append(out.data(), out.length());
            return sqlite_move_ptr(prefixed);
        }
        return sqlite_move_ptr(out);
    }
};
```

### Step 3: Register Companion Functions & Stateful Aggregate
```cpp
void setup_stateful_aggregates(SqliteDatabaseView db) {
    // 1. Initialize per-database shared state
    SqliteExtState<MetricSharedState>::get_or_create(db.get(), [](MetricSharedState* s) {
        s->total_aggregations = 0;
        const char* tag = "BATCH_A";
        memcpy(s->prefix_tag, tag, strlen(tag) + 1);
    });

    // 2. Register aggregate bound to shared state
    SqliteAggregate::define_with_state<MetricSharedState, TaggedConcat>(db, "tagged_concat", 1);
    // Or via umbrella: SqliteExt::define_aggregate_with_state<MetricSharedState, TaggedConcat>(db, "tagged_concat", 1);
}
```

```sql
SELECT department, tagged_concat(employee_name) 
FROM employees 
GROUP BY department;
-- Output: "BATCH_A:Alice, Bob, Charlie"
```

---

## 4. API Reference

### `SqliteAggregateBase<ReturnType = void>`
Base template class for all custom aggregate implementations:

| Base Class | Purpose | Primary Method to Implement |
| :--- | :--- | :--- |
| `SqliteAggregateBase<T>` | Typed return value | `T finalize()` or `T finalize(SqliteContext ctx)` |
| `SqliteAggregateBase<void>` | Direct context control | `void finalize(SqliteContext ctx)` |

### Registration Methods
| Method | Description |
| :--- | :--- |
| `SqliteAggregate::define<T>(db, name, num_args = -1, deterministic = true)` | Registers a stateless aggregate struct. |
| `SqliteAggregate::define_with_state<State, T>(db, name, num_args = -1, deterministic = false)` | Registers a stateful aggregate bound to `SqliteExtState<State>`. |
| `SqliteExt::define_aggregate<T>(db, name, num_args = -1, deterministic = true)` | Umbrella helper for stateless aggregate registration. |
| `SqliteExt::define_aggregate_with_state<State, T>(db, name, num_args = -1, deterministic = false)` | Umbrella helper for stateful aggregate registration. |

### Supported Struct Signatures

#### `step` Overloads
- `void step(SqliteUdfArgs args)`
- `void step(SqliteContext ctx, SqliteUdfArgs args)`
- `void step(sqlite3_context* ctx, SqliteUdfArgs args)`

#### `finalize` Overloads
- `TReturn finalize(SqliteContext ctx)`: Context-aware typed return (Priority 0).
- `void finalize(SqliteContext ctx)`: Context-aware manual result setting (Priority 1).
- `TReturn finalize()`: Stateless auto-converted return (Priority 2).
- `void finalize()`: Stateless void return (Priority 3).

---

## 5. Deep-Dive Architecture Documentation

For memory layouts, alignment guarantees, 4-tier tag dispatching, and empty-set handling:
- **[`docs/AGGREGATE_ARCHITECTURE.md`](AGGREGATE_ARCHITECTURE.md)**: Deep dive into `sqlite3_aggregate_context` storage, SFINAE return rank dispatching, and RAII destruction.
