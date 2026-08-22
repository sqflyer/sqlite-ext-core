# C++ User-Defined Function Builder (`sqlite3_udf.hpp`)

A lightweight, zero-dependency C++ framework for registering and executing SQLite User-Defined Functions (UDFs). It eliminates the boilerplate of manual argument indexing, C-style casts, and `sqlite3_user_data` pointer management while maintaining 100% zero-allocation performance and bounds safety.

## Features
- **Zero Boilerplate**: Register scalar functions in a single line with `SqliteUdf::define(db, "name", num_args, func)`.
- **Bounds-Safe Argument Wrapper (`SqliteUdfArgs`)**: Wraps `(int argc, sqlite3_value** argv)` with index validation. Out-of-bounds indexing safely yields `SQLITE_NULL` views rather than causing segmentation faults.
- **Zero-Allocation Argument Views**: `args[i]` instantly yields a transient `SqliteValueView`, eliminating heap allocations or conversions.
- **Seamless C++11 Lambda Support**: Pass inline stateless lambdas or standard C++ function pointers directly to registration calls.
- **Variadic Function Support**: Register dynamic-arity functions (`num_args = -1`) and query `args.size()` dynamically.
- **Synergy with Value Keys**: Fully interoperates with `SqliteStringOwned`, `SqliteBlobOwned`, and heterogeneous operators for return values and in-place comparisons.
- **Freestanding & `-nostdlib++` Compatible**: Requires no standard library dependencies (no `<functional>`, `<tuple>`, or `<vector>`), making it ideal for embedded and WebAssembly extensions.

## Setup
Include the header in your C++ SQLite extension project:
```cpp
#include "include/sqlite3_udf.hpp"
```

---

## Examples of Usage

### 1. Basic Scalar Function
```cpp
#include "sqlite3_udf.hpp"

// Define a function that adds two numbers
void add_numbers(sqlite3_context* ctx, SqliteUdfArgs args) {
    if (args.size() != 2) {
        sqlite3_result_error(ctx, "add_numbers requires 2 arguments", -1);
        return;
    }

    sqlite3_int64 a = args[0].as_int64();
    sqlite3_int64 b = args[1].as_int64();
    sqlite3_result_int64(ctx, a + b);
}

// Register with the database connection
void register_extension_functions(sqlite3* db) {
    SqliteUdf::define(db, "add_numbers", 2, add_numbers);
}
```

### 2. Inline Stateless C++11 Lambdas
You can register stateless lambdas directly without writing separate named functions:
```cpp
SqliteUdf::define(db, "square", 1, [](sqlite3_context* ctx, SqliteUdfArgs args) {
    if (args.size() != 1) return;
    sqlite3_int64 val = args[0].as_int64();
    sqlite3_result_int64(ctx, val * val);
});
```

### 3. Dynamic Variadic Functions
Pass `-1` as `num_args` to accept any number of parameters:
```cpp
void sum_all(sqlite3_context* ctx, SqliteUdfArgs args) {
    double total = 0.0;
    for (int i = 0; i < args.size(); i++) {
        if (args[i].type() == SQLITE_INTEGER) {
            total += args[i].as_int64();
        } else if (args[i].type() == SQLITE_FLOAT) {
            total += args[i].as_double();
        }
    }
    sqlite3_result_double(ctx, total);
}

// Register variadic function
SqliteUdf::define(db, "sum_all", -1, sum_all);
```

### 4. String Building and Returning
Combine `SqliteUdfArgs` with `SqliteStringOwned` for zero-overhead string manipulation:
```cpp
void repeat_text(sqlite3_context* ctx, SqliteUdfArgs args) {
    if (args.size() != 2) return;

    const char* str = reinterpret_cast<const char*>(sqlite3_value_text(const_cast<sqlite3_value*>(args[0].get())));
    int len = sqlite3_value_bytes(const_cast<sqlite3_value*>(args[0].get()));
    int count = args[1].as_int64();

    SqliteStringOwned result(ctx);
    for (int i = 0; i < count; i++) {
        result.append(str, len);
    }
    
    // Result returned directly to SQLite context
    result.result(ctx);
}

SqliteUdf::define(db, "repeat_text", 2, repeat_text);
```

### 5. In-UDF Heterogeneous Comparisons
Because `args[i]` produces a `SqliteValueView`, you can perform direct comparisons against C++ primitives and string views inside your UDF logic:
```cpp
void classify_val(sqlite3_context* ctx, SqliteUdfArgs args) {
    if (args.size() != 1) return;

    // Heterogeneous operator overloads in action:
    if (args[0] == 42) {
        sqlite3_result_text(ctx, "magic_int", -1, SQLITE_STATIC);
    } else if (args[0] == 3.14) {
        sqlite3_result_text(ctx, "magic_pi", -1, SQLITE_STATIC);
    } else if (args[0] == SqliteStringView("sqlite", 6)) {
        sqlite3_result_text(ctx, "magic_string", -1, SQLITE_STATIC);
    } else {
        sqlite3_result_text(ctx, "other", -1, SQLITE_STATIC);
    }
}
```

---

## API Reference

### `SqliteUdfArgs`
| Method | Return Type | Description |
| :--- | :--- | :--- |
| `size() const` | `int` | Returns the total argument count passed from SQLite. |
| `operator[](int index) const` | `SqliteValueView` | Returns a non-owning wrapper over `argv[index]`. Returns a `SQLITE_NULL` view if `index < 0` or `index >= size()`. |

### `SqliteUdf`
| Method | Return Type | Description |
| :--- | :--- | :--- |
| `define(db, name, num_args, func, deterministic = true)` | `int` | Registers a C++ scalar function or stateless lambda with SQLite. |
