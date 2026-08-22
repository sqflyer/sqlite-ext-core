# C++ Aggregate Function Framework (`sqlite3_aggregate.hpp`)

A zero-dependency, type-safe C++ framework for defining SQLite Aggregate Functions using clean, object-oriented structs. It eliminates the boilerplate of raw C pointers, manual `sqlite3_aggregate_context` memory tracking, and C-style casts while remaining 100% freestanding and `-nostdlib++` compliant.

## Features
- **Object-Oriented Aggregate Structs**: Define custom aggregate states as intuitive C++ structs with `step()` and `finalize()` methods.
- **Single-Line Registration**: Register aggregates effortlessly via `SqliteUdf::define_aggregate<MyAgg>(db, "my_agg", num_args)` or `SqliteAggregate<MyAgg>::define(db, "my_agg", num_args)`.
- **Automatic Return Type Dispatching**: Return standard primitives (`int`, `sqlite3_int64`, `double`, `bool`), `const char*`, or zero-overhead SQLite wrappers (`SqliteStringOwned`, `SqliteBlobOwned`, `SqliteValueOwned`) directly from `finalize()`.
- **Bounds-Safe Parameter Access**: `step(SqliteUdfArgs args)` provides bounds-checked, zero-allocation `SqliteValueView` access.
- **Context-Aware Overloads**: Supports `step(sqlite3_context* ctx, SqliteUdfArgs args)` and `void finalize(sqlite3_context* ctx)` for custom error reporting or direct result setting.
- **RAII Lifecycle & Destructor Cleanup**: Automatically constructs the struct in-place on the first row and deterministically triggers `~T()` when SQLite finishes aggregation.
- **Safe Empty-Set Aggregation**: Handles empty table sets gracefully by finalizing a default-constructed instance without crashing.
- **Freestanding & `-nostdlib++` Ready**: Compiles cleanly with `-fno-exceptions -fno-rtti -nostdlib++`.

## Setup
Include the header in your C++ SQLite extension project:
```cpp
#include "include/sqlite3_aggregate.hpp"
// Or via sqlite3_udf.hpp
#include "include/sqlite3_udf.hpp"
```

---

## Examples of Usage

### 1. Basic Numeric Aggregate (Custom Average)
```cpp
#include "sqlite3_aggregate.hpp"

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
void register_aggregates(sqlite3* db) {
    SqliteUdf::define_aggregate<MyAvg>(db, "my_avg", 1);
}
```

### 2. Dynamic String Concatenation (`SqliteStringOwned`)
```cpp
struct GroupConcat : public SqliteAggregateBase<SqliteStringOwned> {
    SqliteStringOwned str;
    bool first = true;

    void step(SqliteUdfArgs args) override {
        if (args[0].type() == SQLITE_TEXT) {
            if (!first) str.append(", ", 2);
            first = false;
            
            const char* text = reinterpret_cast<const char*>(sqlite3_value_text(const_cast<sqlite3_value*>(args[0].get())));
            int len = sqlite3_value_bytes(const_cast<sqlite3_value*>(args[0].get()));
            str.append(text, len);
        }
    }

    SqliteStringOwned finalize() override {
        return sqlite_move_ptr(str);
    }
};

SqliteUdf::define_aggregate<GroupConcat>(db, "group_concat_custom", 1);
```

### 3. Binary Blob Accumulator (`SqliteBlobOwned`)
```cpp
struct BlobCollector : public SqliteAggregateBase<SqliteBlobOwned> {
    unsigned char buffer[256];
    int size = 0;

    void step(SqliteUdfArgs args) override {
        if (args[0].type() == SQLITE_BLOB) {
            const void* data = sqlite3_value_blob(const_cast<sqlite3_value*>(args[0].get()));
            int bytes = sqlite3_value_bytes(const_cast<sqlite3_value*>(args[0].get()));
            if (size + bytes <= 256) {
                memcpy(buffer + size, data, bytes);
                size += bytes;
            }
        }
    }

    SqliteBlobOwned finalize() override {
        return SqliteBlobOwned(buffer, size);
    }
};

SqliteUdf::define_aggregate<BlobCollector>(db, "blob_accum", 1);
```

### 4. Multi-Argument Aggregates (Weighted Average)
```cpp
struct WeightedAvg : public SqliteAggregateBase<double> {
    double weighted_sum = 0.0;
    double total_weight = 0.0;

    void step(SqliteUdfArgs args) override {
        if (args.size() >= 2) {
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

SqliteUdf::define_aggregate<WeightedAvg>(db, "weighted_avg", 2);
```

### 5. Context-Aware Error Reporting
```cpp
struct StrictPositiveSum : public SqliteAggregateBase<void> {
    sqlite3_int64 sum = 0;
    bool has_error = false;

    void step(sqlite3_context* ctx, SqliteUdfArgs args) override {
        sqlite3_int64 val = args[0].as_int64();
        if (val < 0) {
            has_error = true;
            sqlite3_result_error(ctx, "Negative numbers are disallowed", -1);
            return;
        }
        sum += val;
    }

    void finalize(sqlite3_context* ctx) override {
        if (!has_error) {
            sqlite3_result_int64(ctx, sum);
        }
    }
};

SqliteUdf::define_aggregate<StrictPositiveSum>(db, "strict_sum", 1);
```

---

## API Reference

### `SqliteAggregateBase<ReturnType = void>`
Base template class for all custom aggregate implementations. User structs must publicly inherit from `SqliteAggregateBase<ReturnType>` (e.g. `SqliteAggregateBase<double>`, `SqliteAggregateBase<SqliteStringOwned>`, or `SqliteAggregateBase<void>`).

| Base Class | Purpose | Required Method to Implement |
| :--- | :--- | :--- |
| `SqliteAggregateBase<T>` | Typed return value | `T finalize() override` |
| `SqliteAggregateBase<void>` | Direct context control | `void finalize(sqlite3_context* ctx) override` |

### `SqliteAggregate<T>` / `SqliteUdf::define_aggregate<T>`
| Function | Return Type | Description |
| :--- | :--- | :--- |
| `SqliteAggregate<T>::define(db, name, num_args = -1, deterministic = true)` | `int` | Registers aggregate struct `T` with SQLite. |
| `SqliteUdf::define_aggregate<T>(db, name, num_args = -1, deterministic = true)` | `int` | Convenience forwarder on `SqliteUdf`. |

### Supported Aggregate Struct Signatures

#### `step` Overloads
- `void step(SqliteUdfArgs args)`
- `void step(sqlite3_context* ctx, SqliteUdfArgs args)`

#### `finalize` Overloads
- `TResult finalize()`: Automatically dispatches result for `int`, `sqlite3_int64`, `double`, `bool`, `const char*`, `SqliteStringView`, `SqliteStringOwned`, `SqliteBlobView`, `SqliteBlobOwned`, `SqliteValueView`, `SqliteValueOwned`.
- `void finalize(sqlite3_context* ctx)`: Manually sets result or error on the SQLite context.

---

For architectural details, please see [AGGREGATE_ARCHITECTURE.md](AGGREGATE_ARCHITECTURE.md).
