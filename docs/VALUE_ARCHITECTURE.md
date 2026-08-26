# C++ Value Types Architecture (`sqlite3_value.hpp`)

This document details the internal design, Small Buffer Optimization (SBO) memory layouts, view extraction mechanisms, and heterogeneous comparison operator suites implemented by `sqlite3_value.hpp`.

> **API & Usage Guide**: For usage tutorials, examples, and the public API reference, see [`docs/VALUE_README.md`](VALUE_README.md).

---

## 1. Architectural Motivation: The View vs Owned Paradigm

When SQLite executes queries or passes parameters to User-Defined Functions (UDFs), it passes raw pointers (`sqlite3_value*`). Traditional C++ extension development suffers from two critical bottlenecks:

1. **Unnecessary Heap Allocations**: Converting a transient `sqlite3_value*` into a standard C++ type (like `std::string`) invokes `malloc()` just to perform a lookup in a `std::map` or pass it to a sub-routine.
2. **Polymorphic Variant Overhead**: Standard variants (`std::variant`, `boost::variant`) have high footprint, require exceptions/RTTI, and do not follow SQLite's exact collation semantics (`NULL < NUMERIC < TEXT < BLOB`).

`sqlite3_value.hpp` solves this via the **View vs Owned** architectural model:

```
+---------------------------------------------------------------------------------------+
|                                    VIEW TYPES                                         |
|  - Zero dynamic allocations (lives entirely on the stack or in CPU registers)         |
|  - Non-owning transient wrappers over raw SQLite memory                               |
|                                                                                       |
|  SqliteValueView                SqliteStringView               SqliteBlobView         |
|  [sqlite3_value* (8B)]          [const char* (8B), len (4B)]   [const void* (8B), (4B)]|
+---------------------------------------------------------------------------------------+
                                           ^
                     .as_text() / .as_blob() extraction
                                           |
+---------------------------------------------------------------------------------------+
|                                   OWNED TYPES                                         |
|  - RAII memory ownership & lifecycle management                                       |
|  - Small Buffer Optimization (SBO) for numeric primitives                             |
|  - Dynamic heap allocation for Strings & Blobs via SQLite allocators                  |
|                                                                                       |
|  SqliteValueOwned               SqliteStringOwned              SqliteBlobOwned        |
|  [type (4B), Union (8B)]        [sqlite3_str* (8B)]            [void* (8B), len (4B)] |
+---------------------------------------------------------------------------------------+
```

---

## 2. Memory Layout & Small Buffer Optimization (SBO)

### `SqliteValueOwned` Memory Structure
`SqliteValueOwned` uses a tagged union to store numeric primitives completely inline, bypassing heap allocation entirely:

```
+-------------------+-------------------+---------------------------------------+
|  m_type (4 bytes) | (4 bytes padding) |          m_data (8-byte Union)        |
|   SQLITE_INTEGER  |                   |   iValue (sqlite3_int64: 8 bytes)     |
|   SQLITE_FLOAT    |                   |   dValue (double: 8 bytes)            |
|   SQLITE_TEXT     |                   |   pValue (sqlite3_value*: 8 bytes)    |
|   SQLITE_BLOB     |                   |                                       |
|   SQLITE_NULL     |                   |                                       |
+-------------------+-------------------+---------------------------------------+
<----------------------------------- 16 bytes Total ----------------------------------->
```

### Union Safety & `heap_value()` Discrimination
Because `m_data` is a union, accessing `m_data.pValue` when `m_type == SQLITE_INTEGER` or `SQLITE_FLOAT` reads numeric bits as a pointer, causing undefined behavior.

To ensure safety:
```cpp
const sqlite3_value* heap_value() const {
    return (m_type == SQLITE_TEXT || m_type == SQLITE_BLOB) ? m_data.pValue : nullptr;
}
```
All view extractions (`as_text()`, `as_blob()`) and destructions route through `heap_value()`, preventing union memory corruption.

---

## 3. Zero-Allocation View Extraction Architecture

`as_text()` and `as_blob()` extract lightweight non-owning views directly from values:

```
                          +-------------------------------+
                          | SqliteValueView / Owned       |
                          +---------------+---------------+
                                          |
                +-------------------------+-------------------------+
                | .as_text()                                        | .as_blob()
                v                                                   v
+-------------------------------+                   +-------------------------------+
| Check: heap_value() or m_val  |                   | Check: heap_value() or m_val  |
+---------------+---------------+                   +---------------+---------------+
                |                                                   |
        +-------+-------+                                   +-------+-------+
        | Valid         | NULL / Missing                    | Valid         | NULL / Missing
        v               v                                   v               v
+---------------+ +-------------+                   +---------------+ +-------------+
| SqliteString- | | StringView  |                   | SqliteBlob-   | | BlobView    |
| View(ptr, len)| | (nullptr, 0)|                   | View(ptr, len)| | (nullptr, 0)|
+---------------+ +-------------+                   +---------------+ +-------------+
```

---

## 4. Heterogeneous Comparison Operator Suite

To enable transparent map lookups (`std::map<SqliteValueOwned, T, std::less<>>` or `std::unordered_map`), `sqlite3_value.hpp` generates over **144 inline comparison operators**:

- **Strict Weak Ordering**: Guarantees consistent ordering across different types using the SQLite hierarchy:
  $$\text{NULL} < \text{NUMERIC} < \text{TEXT} < \text{BLOB}$$
- **NaN Stability**: Floating-point `NaN` values are strictly sorted to the front of the numeric partition, preventing binary search tree corruption in `std::map`.
- **Type Collision Tie-Breakers**: When an integer and a float share the same numerical value (e.g. `5` and `5.0`), their type IDs break the tie, preserving strict typing.

---

## 5. Freestanding Memory Guarantees

All classes strictly adhere to `-nostdlib++` requirements:
- Memory for `SqliteStringOwned` is allocated via `sqlite3_str_new(nullptr)`.
- Memory for `SqliteBlobOwned` is allocated via `sqlite3_malloc`.
- Dynamic values in `SqliteValueOwned` are duplicated via `sqlite3_value_dup` and freed via `sqlite3_value_free`.
- Zero standard library exceptions or RTTI dependencies are introduced.

---

## 6. OOM Hardening & Non-Throwing Validity Semantics

Because the framework operates with exceptions disabled (`-fno-exceptions` / `/EHs-c-`), constructors cannot throw on allocation failure:

1. **Deterministic Error States**:
   - If `sqlite3_value_dup()` returns `nullptr` on memory exhaustion, `SqliteValueOwned` records `m_data.pValue = nullptr`.
   - If `sqlite3_malloc()` fails for `SqliteBlobOwned`, `m_data` is set to `nullptr` and `m_size` to `0`.
   - If `sqlite3_str_new()` fails or encounters an allocation fault, `errcode()` reflects `SQLITE_NOMEM`.
2. **Safe Operation Degradation**: Calling `.result()` or `.bind()` on an invalid or OOM value safely returns `SQLITE_NOMEM` or emits an error context without dereferencing null pointers.
3. **Explicit Inspection**: All owned wrappers provide `is_valid()` and `explicit operator bool()` for deterministic call-site error checking.
