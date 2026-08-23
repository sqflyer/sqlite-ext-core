# C++ Value Types (`sqlite3_value.hpp`)

Zero-dependency C++ RAII wrappers for SQLite core data types, engineered specifically to enable zero-allocation heterogeneous map lookups, safe polymorphic variants, zero-cost view extractions (`as_text()`, `as_blob()`), and seamless UDF/statement integration.

> **Architecture Reference**: For an in-depth breakdown of Small Buffer Optimization (SBO) memory layouts, tagged union safety, and the 144+ heterogeneous relational operator suite, see [`docs/VALUE_ARCHITECTURE.md`](VALUE_ARCHITECTURE.md).

---

## 1. Features Matrix

| Feature | Description |
| :--- | :--- |
| **Zero-Allocation Views** | Non-owning wrappers (`SqliteStringView`, `SqliteBlobView`, `SqliteValueView`) that never allocate heap memory during statement reads, UDF argument checks, or map lookups. |
| **Small Buffer Optimization (SBO)** | `SqliteValueOwned` stores primitives (`SQLITE_INTEGER`, `SQLITE_FLOAT`) inline in a 16-byte union, bypassing heap allocation entirely. |
| **Direct View Extraction** | `as_text()` and `as_blob()` provide zero-allocation `SqliteStringView` and `SqliteBlobView` directly from both `SqliteValueView` and `SqliteValueOwned`. |
| **Heterogeneous Lookups** | 144+ macro-generated operator overloads across `String`, `Blob`, and C++ primitives (`int`, `double`, `sqlite3_int64`) using all 6 standard relational operators (`==`, `!=`, `<`, `>`, `<=`, `>=`). |
| **Polymorphic Variants** | Safely store Integers, Floats, Strings, and Blobs in the same `std::map<SqliteValueOwned, T>` with strict SQLite collation order (`NULL < NUMERIC < TEXT < BLOB`). |
| **Ergonomic String Builders** | `SqliteStringOwned` dynamically builds strings using SQLite's native allocator (`sqlite3_str_new`), directly transferable to `SqliteContext` or statements. |
| **SQLite Lifecycle Helpers** | `.bind(stmt, col)` and `.result(ctx)` methods directly transfer results with automated memory ownership management. |
| **Freestanding & `-nostdlib++`** | 100% header-only, zero dependencies on standard library runtime heaps (`<string>`, `<vector>`, `<memory>`). |

---

## 2. Value Views vs Owned Types

```
+-----------------------------------------------------------------------------+
|                               VIEW CLASSES                                  |
| (Non-owning, Zero-Allocation, Transient wrappers over SQLite raw pointers)  |
|                                                                             |
|   SqliteValueView        SqliteStringView             SqliteBlobView        |
|  (sqlite3_value*)     (const char*, int len)       (const void*, int len)   |
+-----------------------------------------------------------------------------+
                                     |
               Extract with .as_text() / .as_blob()
                                     v
+-----------------------------------------------------------------------------+
|                               OWNED CLASSES                                 |
| (RAII memory management, Small Buffer Optimization, Automatic destruction)  |
|                                                                             |
|   SqliteValueOwned       SqliteStringOwned            SqliteBlobOwned       |
|  (SBO union / heap)    (sqlite3_str dynamic)       (sqlite3_malloc bytes)   |
+-----------------------------------------------------------------------------+
```

---

## 3. Direct View Extraction: `as_text()` and `as_blob()`

Both `SqliteValueView` and `SqliteValueOwned` expose zero-allocation accessors that return non-owning views:

```cpp
#include "sqlite3_value.hpp"

void process_value(SqliteValueView val) {
    // 1. Extract numeric primitives
    if (val.type() == SQLITE_INTEGER) {
        sqlite3_int64 num = val.as_int64();
    } else if (val.type() == SQLITE_FLOAT) {
        double d = val.as_double();
    }

    // 2. Extract String View (Zero Allocation)
    if (val.type() == SQLITE_TEXT) {
        SqliteStringView str = val.as_text();
        printf("Text: %.*s (len=%d)\n", str.length(), str.data(), str.length());
    }

    // 3. Extract Blob View (Zero Allocation)
    if (val.type() == SQLITE_BLOB) {
        SqliteBlobView blob = val.as_blob();
        printf("Blob size: %d bytes\n", blob.size());
    }
}
```

### Safety Guarantees:
- **Null Safety**: If called on a `NULL` value or a `nullptr` view, `as_text()` safely returns `SqliteStringView(nullptr, 0)` and `as_blob()` safely returns `SqliteBlobView(nullptr, 0)` without crashing.
- **SBO Union Safety**: Calling `as_text()` or `as_blob()` on an owned integer or float correctly inspects `heap_value()`, preventing invalid pointer dereferences.

---

## 4. Heterogeneous Map Lookups (Zero-Allocation)

Using `std::map<SqliteStringOwned, MyData, std::less<>>` allows storing strings securely, but looking them up using transient SQLite values or string literals without any dynamic memory allocations:

```cpp
#include <map>
#include "sqlite3_value.hpp"

// 1. Map with C++14 heterogeneous comparator: std::less<>
std::map<SqliteStringOwned, int, std::less<>> user_scores;

// 2. Insert data (SqliteStringOwned allocates via sqlite3_malloc)
user_scores.emplace(SqliteStringOwned("alice"), 100);
user_scores.emplace(SqliteStringOwned("bob"), 200);

// 3. Zero-Allocation Lookup inside a UDF:
void score_lookup_udf(SqliteContext ctx, SqliteUdfArgs args) {
    // args[0].as_text() returns SqliteStringView (Zero Allocation)
    SqliteStringView key = args[0].as_text();

    auto it = user_scores.find(key); // Instant lookup! Zero heap allocations
    if (it != user_scores.end()) {
        ctx.result_int(it->second);
    } else {
        ctx.result_null();
    }
}
```

---

## 5. Polymorphic Variant Maps

Store Integers, Floats, Strings, and Blobs in the same `std::map` using `SqliteValueOwned`. Keys are automatically sorted following SQLite's native collation hierarchy:

$$\text{NULL} < \text{NUMERIC} < \text{TEXT} < \text{BLOB}$$

```cpp
std::map<SqliteValueOwned, const char*, std::less<>> poly_registry;

// Store diverse types seamlessly
poly_registry.emplace(SqliteValueOwned(42), "The Answer");
poly_registry.emplace(SqliteValueOwned(3.14), "Pi");
poly_registry.emplace(SqliteValueOwned("config_key"), "App Config");

// Transparent lookups using native primitives or Views:
auto it1 = poly_registry.find(42);              // Finds via integer overload
auto it2 = poly_registry.find("config_key");     // Finds via string view overload
```

---

## 6. UDF and Statement Lifecycle Integration

All wrappers provide `.bind()` and `.result()` methods to interface directly with `SqliteContext` and `sqlite3_stmt*`:

```cpp
// Return results directly to a UDF context:
SqliteStringOwned str(ctx.get());
str.appendall("Result: ");
str.appendall("OK");
str.result(ctx); // Safely sets result and transfers ownership!

// Bind directly to prepared statements:
SqliteBlobOwned blob(raw_bytes, 16);
blob.bind(stmt, 1); // Binds as SQLITE_TRANSIENT
```

---

## 7. Deep-Dive Architecture Documentation

For complete internal design details, memory layouts, and algorithmic mechanics:
- **[`docs/VALUE_ARCHITECTURE.md`](VALUE_ARCHITECTURE.md)**: Deep dive into Small Buffer Optimization (SBO) 16-byte memory union layouts, `heap_value()` union safety, the zero-allocation view extraction pipeline, and the 144+ operator heterogeneous lookup engine.

