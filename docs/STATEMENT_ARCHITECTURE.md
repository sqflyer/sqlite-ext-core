# C++ RAII Statement Wrapper Architecture (`sqlite3_statement.hpp`)

This document details the internal design and architectural decisions behind `sqlite3_statement.hpp`.

---

## Architectural Objectives

1. **Deterministic Resource Management**: Prevent statement leaks (`sqlite3_stmt` unfreed handles) in complex control flow without exceptions.
2. **Move-Only Lifetime Semantics**: Prohibit accidental shallow copies of underlying SQLite VDBE bytecode cursors while enabling efficient transfer across function returns and containers.
3. **Zero Dynamic Allocation**: Parameter bindings and column reads must operate with zero heap overhead and zero C++ runtime overhead.
4. **Deep Interoperability**: Seamlessly integrate with `sqlite3_value.hpp` (`SqliteStringView`, `SqliteBlobView`, `SqliteValueView`, `SqliteValueOwned`).

---

## Move-Only Resource Ownership Pattern

SQLite prepared statements are stateful, single-owner cursors in SQLite's virtual machine (VDBE). Copying a `sqlite3_stmt*` leads to double-finalization or concurrent cursor step corruption.

`SqliteStatement` enforces strict unique ownership:

```cpp
// Explicitly deleted copy operations
SqliteStatement(const SqliteStatement&) = delete;
SqliteStatement& operator=(const SqliteStatement&) = delete;

// Move constructor transfers raw pointer and nullifies source
inline SqliteStatement(SqliteStatement&& other) noexcept : m_stmt(other.m_stmt) {
    other.m_stmt = nullptr;
}

// Move assignment finalizes previous statement before adopting new handle
inline SqliteStatement& operator=(SqliteStatement&& other) noexcept {
    if (this != &other) {
        finalize();
        m_stmt = other.m_stmt;
        other.m_stmt = nullptr;
    }
    return *this;
}
```

### Cached Statement Leasing (Polymorphic Ownership)

To support high-performance, tight-loop execution (like scalar UDFs), the architecture includes `SqliteCachedStatement`. This derived class relies on C++'s static destruction order to fundamentally alter the lifecycle without adding `virtual` overhead:

```cpp
class SqliteCachedStatement : public SqliteStatement {
    ~SqliteCachedStatement() {
        sqlite3_stmt* stmt = release(); // Base class sets m_stmt = nullptr
        if (stmt) {
            sqlite3_clear_bindings(stmt);
            sqlite3_reset(stmt);
        }
    }
};
```

By intentionally nullifying `m_stmt` in the derived destructor (`release()`), the compiler automatically calls the base `~SqliteStatement()` destructor, which safely observes `m_stmt == nullptr` and **skips finalization**. This cleanly detaches the active bytecode cursor and scrubs it for reuse, perfectly modeling a "Cache Owner → Temporary Lease" architecture.

---

## Zero-Allocation Column Extraction Pipeline

When extracting column values from an active `SQLITE_ROW`, conventional C++ libraries allocate `std::string` or `std::vector<uint8_t>`.

`SqliteStatement` pairs directly with the lightweight non-owning view wrappers from `sqlite3_value.hpp`:

```
+--------------------------+
|  Active VDBE Row Record  |
+--------------------------+
          |
          +---> sqlite3_column_text() ----> SqliteStringView (ptr, len) [0 Heap Allocations]
          |
          +---> sqlite3_column_blob() ----> SqliteBlobView   (ptr, len) [0 Heap Allocations]
          |
          +---> sqlite3_column_value() ---> SqliteValueView  (sqlite3_value*) [0 Heap Allocations]
          |
          +---> sqlite3_column_value() ---> SqliteValueOwned (SBO union / dup) [Optimized Heap Copy]
```

- **`column_string_view(col)`**: Captures the SQLite internal memory pointer and byte count directly into a `SqliteStringView`.
- **`column_blob_view(col)`**: Captures the binary memory buffer into a `SqliteBlobView`.
- **`column_value_view(col)`**: Wraps `sqlite3_column_value()` into a polymorphic transient view.

---

## Ergonomic Execution Cycles

### 1. Multi-Row Queries
```cpp
SqliteStatement query(db, "SELECT x FROM data;");
while (query.next()) {
    // Process row
}
```
`next()` returns `true` exclusively on `SQLITE_ROW` and cleanly terminates on `SQLITE_DONE` or any error condition.

### 2. DML / DDL Fast-Path (`execute()`)
```cpp
SqliteStatement insert(db, "INSERT INTO t VALUES (?);");
insert.bind(1, 42);
insert.execute(); // Performs step() + reset() atomically
```
`execute()` handles the common pattern of single-step execution and resets the statement for immediate reuse without requiring manual calls to `sqlite3_reset()`.

---

## Exception-Free Error Handling

Because extensions must be buildable with `-fno-exceptions`, `SqliteStatement` avoids throwing C++ exceptions:
- Lifecycle methods return raw SQLite error codes (`SQLITE_OK`, `SQLITE_ERROR`, `SQLITE_MISUSE`).
- Unprepared instances safely return `SQLITE_MISUSE` on operations and return safe default representations (e.g. `nullptr`, `0`, `false`) on column queries.
