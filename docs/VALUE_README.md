# C++ Value Types (`sqlite3_value.hpp`)

Zero-dependency C++ RAII wrappers for SQLite core data types, engineered specifically to enable zero-allocation heterogeneous map lookups, safe polymorphic variants, and seamless UDF/statement integration.

## Features
- **Zero-Allocation Lookups**: Provides non-owning `View` wrappers (`SqliteStringView`, `SqliteBlobView`, `SqliteValueView`) to prevent expensive memory allocations during C++ map lookups, statement column reads, and UDF argument access.
- **Small Buffer Optimization (SBO)**: `SqliteValueOwned` utilizes a memory union to store primitives (Integer, Float) completely inline, bypassing heap allocation entirely.
- **Heterogeneous Lookups**: Natively supports 144+ macro-generated operator overloads for deep heterogeneous lookup support across `String`, `Blob`, and C++ primitives (`int`, `double`, `sqlite3_int64`) using all 6 standard relational operators (`==`, `!=`, `<`, `>`, `<=`, `>=`).
- **Polymorphic Variants**: Safely store Integer, Float, Text, and Blob payloads inside the exact same `std::map` using the polymorphic `SqliteValueOwned` wrapper. Strict Weak Ordering guarantees flawless type-safety and stable `NaN` sorting.
- **Ergonomic String Builders**: Easily construct dynamic strings without a database handle using standard `(const char*)` constructors, or safely instantiate them inside User-Defined Functions with `(sqlite3_context*)` wrappers.
- **SQLite Integration**: Provides `bind()` and `result()` methods directly on wrappers to easily interoperate with `sqlite3_stmt` parameters and `sqlite3_context` returns.
- **Accurate & Fast Collation**: Follows exact SQLite collation semantics (`NULL < NUMERIC < TEXT < BLOB`) and accelerates lexicographical comparisons using SIMD-optimized `memcmp` routines.
- **Zero STL Overhead**: Fully implemented using raw C-pointers, `<string.h>`, and SQLite's native memory profilers (`sqlite3_malloc`). No `<string>`, `<vector>`, or `<cstring>` overhead. Perfect for constrained environments like WASM.

## Setup
Simply `#include "include/sqlite3_value.hpp"` in your SQLite C++ extension project!

## Examples of Usage

### 1. Heterogeneous Map Lookups (Zero-Allocation)
Using `std::map<SqliteStringOwned, MyData, std::less<>>` allows you to store strings securely, but look them up using a transient `sqlite3_value` without ever allocating memory.

```cpp
// 1. Create your map with the C++14 heterogeneous lookup comparator: std::less<>
std::map<SqliteStringOwned, int, std::less<>> my_map;

// 2. Insert data by copying memory (SqliteStringOwned)
my_map.emplace(SqliteStringOwned("my_key_1"), 100);
my_map.emplace(SqliteStringOwned("my_key_2"), 200);

// Inside your UDF scalar function:
static void map_lookup_udf(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    // 3. Create a zero-allocation View from the incoming parameter
    SqliteStringView search_key(argv[0]);

    // 4. Perform the map lookup instantly! No malloc() or std::string construction required.
    auto it = my_map.find(search_key);
    if (it != my_map.end()) {
        sqlite3_result_int(ctx, it->second);
    }
}
```

### 2. Polymorphic Variants
You can store Integers, Floats, Strings, and Blobs in the exact same `std::map` securely using `SqliteValueOwned`. The keys are perfectly sorted according to SQLite's native collation rules (`NULL < NUMERIC < TEXT < BLOB`).

```cpp
std::map<SqliteValueOwned, std::string, std::less<>> poly_map;

// Inside your UDF scalar function inserting data:
static void insert_poly_udf(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    // Securely duplicates the transient sqlite3_value (could be Int, Float, Text, etc.)
    SqliteValueOwned key_to_store(argv[0]);
    poly_map.emplace(std::move(key_to_store), "Stored!");
}

// Inside your UDF searching for data:
static void search_poly_udf(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    // Zero-allocation lookup against the polymorphic map
    SqliteValueView search_key(argv[0]);
    
    auto it = poly_map.find(search_key);
    if (it != poly_map.end()) {
        // ...
    }
}

// You can also instantiate variants from primitives directly (Zero-Allocation via SBO):
SqliteValueOwned my_int(42);
SqliteValueOwned my_float(3.14);

// Heterogeneous Primitive Lookups work natively:
if (my_int == 42) { /* Works! */ }
if (my_int != 42.0) { /* Strict typing: Int(42) != Float(42.0) */ }
```

### 3. Transparent Map Lookups (C++14 `std::less<>`)
By implementing a massive suite of 144+ heterogeneous relational operators, `SqliteValueOwned` is fully compatible with C++14 transparent comparators. 

If you define a map like this:
```cpp
std::map<SqliteValueOwned, MyData, std::less<>> my_map;
```

You can query it instantaneously without ever allocating memory:
```cpp
my_map.find(5);          // Finds the integer 5 natively via macro overloads
my_map.find(3.14);       // Finds the float natively via macro overloads
my_map.find("hello");    // Finds the string (via SqliteStringView implicit conversion)
```

### 4. Transparent Hash Maps (C++20 `std::unordered_map`)
Because `SqliteValueOwned` avoids mixing type-IDs into its hashes for primitives and strings, you can easily implement **Zero-Allocation Heterogeneous Lookups** in hash maps using the built-in transparent functors: `SqliteValueHash` and `SqliteValueEqual`.

```cpp
#include <unordered_map>

// Create a map using the built-in transparent hash and equality functors
std::unordered_map<SqliteValueOwned, int, SqliteValueHash, SqliteValueEqual> my_hash_map;

// Insert data
my_hash_map.emplace("hello", 100);
my_hash_map.emplace(42, 200);

// Lookups require ZERO memory allocation and construct NO temporary objects!
my_hash_map.find("hello"); // Native string lookup
my_hash_map.find(42);      // Native integer lookup

// You can even look them up using dynamic SqliteString buffers!
SqliteString buffer("hello");
my_hash_map.find(buffer);  // Native buffer lookup (hashes match exactly!)
```

### 5. Ergonomic String Builders
Construct strings dynamically and seamlessly without ever managing memory directly.

```cpp
// Works inside UDFs or background threads. Uses sqlite3_malloc under the hood.
SqliteStringOwned builder;
builder.append("Hello ", 6);
builder.appendchar(1, '[');
builder.appendf("%d", 42); // Type-safe snprintf integration
builder.appendchar(1, ']');

// Result natively abstracts away sqlite3_result_text
builder.result(ctx); // Outputs: "Hello [42]"
```

### 6. Fast Binding & Result Wrappers
```cpp
static void bind_example(sqlite3_stmt* stmt, sqlite3_value** argv) {
    // Bind incoming SQLite values directly to a prepared statement
    SqliteValueView val(argv[0]);
    val.bind(stmt, 1); // Binds exactly as the original type (Int, Text, Blob, etc.)
    
    SqliteStringView str("static_text", 11);
    str.bind(stmt, 2);
}
```

For architectural details, please see [VALUE_ARCHITECTURE.md](VALUE_ARCHITECTURE.md).
