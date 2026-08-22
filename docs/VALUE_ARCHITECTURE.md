# C++ Value Types Architecture (`sqlite3_value.hpp`)

## Overview
`c-sqlite-ext-core` provides zero-dependency C++ RAII wrappers over SQLite's core types (`sqlite3_value`, `sqlite3_str`, and binary blobs).

The architecture fundamentally relies on a **View vs Owned** paradigm to solve the classic C++ heterogeneous lookup problem for SQLite maps, User-Defined Functions, and prepared statements.

## The Problem
When SQLite passes parameters to your C++ extension functions, it provides them as transient pointers (e.g. `const sqlite3_value*`). If you wish to use these as keys to search a `std::map` or pass them through application layers, traditional C++ requires you to construct a `std::string` or allocate memory to build the key, causing an expensive memory allocation just to perform a lookup.

## The Solution: View vs Owned

### The `View` Classes
- `SqliteStringView`
- `SqliteBlobView`
- `SqliteValueView`

These classes are **zero-allocation**. They do not copy memory. They merely wrap the raw pointers and length provided by SQLite. Use these to perform instant hash lookups, read statement columns, or pass UDF arguments without ever hitting `malloc()`.

To support C++14 transparent comparators, `SqliteStringView` includes an implicit constructor from `const char*`. This allows queries like `my_map.find("hello")` to seamlessly map to the heterogeneous lookup operators without requiring the user to explicitly instantiate the wrapper.

### The `Owned` Classes
- `SqliteStringOwned`
- `SqliteBlobOwned`
- `SqliteValueOwned`

These classes are memory-managed via **RAII**. They use `sqlite3_malloc`, `sqlite3_value_dup`, or `sqlite3_str_new` to copy the data securely into permanent memory. They automatically free their memory upon destruction.

### Small Buffer Optimization (SBO)
To guarantee performance, `SqliteValueOwned` implements **Small Buffer Optimization (SBO)**. If the `sqlite3_value` being copied is a primitive type (`SQLITE_INTEGER` or `SQLITE_FLOAT`), the data is stored directly inline inside a union buffer. Memory is only heap-allocated (`sqlite3_value_dup`) for dynamically sized types (`SQLITE_TEXT` and `SQLITE_BLOB`).

To ensure maximum ergonomics, `SqliteStringOwned` provides specialized constructors to handle different contexts seamlessly:
- **`sqlite3_context*`**: Automatically extracts the database handle when building strings inside UDFs.
- **`const char*` / default**: Uses `sqlite3_str_new(nullptr)` to allocate strings directly from the global heap via `sqlite3_malloc`, completely eliminating the need for a database handle.

Use these `Owned` classes as the actual values or keys stored inside your maps and long-lived structures.

### The `Util` Namespaces
- `SqliteHashUtil`
- `SqliteStringUtil`
- `SqliteBlobUtil`
- `SqliteValueUtil`

These hold the shared logic for computing `FNV-1a` hashes, equality, and lexicographical less-than comparisons. `SqliteHashUtil` provides a centralized, high-performance inline FNV-1a mixer. To enable seamless `std::unordered_map` heterogeneous lookups, `SqliteValueUtil` explicitly avoids mixing type-IDs into the hash algorithm. This guarantees that the hash of a polymorphic variant perfectly matches the hashes of native C++ primitives and standalone strings. 

This architecture allows developers to instantly achieve zero-allocation heterogeneous hash-map lookups using the two built-in C++20 transparent functors:
- **`SqliteValueHash`**: A transparent hasher for all wrappers and primitives.
- **`SqliteValueEqual`**: A transparent equality struct leveraging the massive `operator==` suite.

To guarantee optimal performance, these comparisons replace manual byte-loops with `SqliteMemoryUtil::memcmp_equal` and `SqliteMemoryUtil::memcmp_less` which rely on SIMD-accelerated C `memcmp`. By exporting these core utilities, the `SqliteBuffer` and `SqliteString` classes in the memory subsystem are able to plug directly into the exact same equality and hashing engine. 

Furthermore, `SqliteValueUtil` implements the official SQLite collation order (`NULL < NUMERIC < TEXT < BLOB`). By centralizing this logic, we enable **Heterogeneous Lookups**, allowing a `View` object (or a `SqliteBuffer`) to directly search and compare against an `Owned` object natively.

## SQLite Integration Helpers
All `View` and `Owned` wrappers provide `bind(sqlite3_stmt*, int col)` and `result(sqlite3_context*)` helper methods. These methods abstract away the `sqlite3_bind_*` and `sqlite3_result_*` C-APIs, making it effortless to return custom payloads from UDFs or bind data into prepared statements using `SQLITE_TRANSIENT` memory lifetimes.

## Polymorphic Variants
The `SqliteValueOwned` and `SqliteValueView` classes act as type-safe polymorphic variant map keys. A map constructed with `std::map<SqliteValueOwned, T>` can safely store and query Integer, Float, Text, and Blob variants simultaneously without type collisions.

- **Massive Macro Generation**: A suite of macros (`SQLITE_DEF_VAL_STR_OPS`, `SQLITE_DEF_VAL_PRIM_OPS`, etc.) generates over 144 inline comparison operators, establishing strict-weak ordering across all 6 relational operators (`==`, `!=`, `<`, `>`, `<=`, `>=`) between values and native types.
- **Transparent Mapping**: Thanks to the comprehensive definition of relational operators and the implicit `SqliteStringView(const char*)` constructor, polymorphic `std::map<SqliteValueOwned, T, std::less<>>` instances can be queried dynamically using native C++ primitives and C-strings with zero overhead.

### Strict Weak Ordering & NaN Stability
Because `std::map` fundamentally relies on Strict Weak Ordering for binary tree navigation, the variant `<` operators implement complex tie-breaker heuristics:
1. **Type Hierarchies**: Follows `NULL < NUMERIC < TEXT < BLOB`.
2. **NaN Stability**: All `NaN` floating point values are statically forced to sort to the front of the tree, ensuring that maps do not fracture when encountering `0.0/0.0`.
3. **Type Tie-Breakers**: An Integer and a Float with identical numeric values (e.g. `5` and `5.0`) resolve collisions using their underlying type-IDs, preventing collisions and maintaining strict type integrity during heterogeneous queries.
