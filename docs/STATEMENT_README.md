# C++ RAII Statement Wrapper (`sqlite3_statement.hpp`)

A zero-dependency, freestanding C++ RAII wrapper over SQLite prepared statements (`sqlite3_stmt*`). It guarantees exception-safe statement finalization, provides fluent and named parameter bindings, and delivers zero-allocation column extractions.

## Features
- **Strict RAII Finalization**: Destructor automatically calls `sqlite3_finalize(m_stmt)`.
- **Move-Only Semantics**: Explicitly non-copyable; implements noexcept move constructors and move assignment operators for safe transfer across scopes.
- **Fluent Parameter Binding**: Single-line binding for C++ primitives (`int`, `int64`, `double`), strings, blobs, and polymorphic SQLite value objects by 1-based index.
- **Named Parameter Support**: Bind directly by SQL parameter name (e.g. `:user_id`, `@score`, `$text`) with automatic index resolution.
- **Zero-Allocation Column Extraction**: Access column outputs directly as lightweight `SqliteStringView`, `SqliteBlobView`, or `SqliteValueView` objects without heap copies.
- **Polymorphic Storage**: Read column values directly into `SqliteValueOwned` objects for insertion into heterogeneous containers or cache layers.
- **Simple Iteration Loop**: Clean `while (stmt.next())` or `while (stmt.step() == SQLITE_ROW)` loop ergonomics.
- **Freestanding & `-nostdlib++` Ready**: Built completely without `<memory>`, `<string>`, or standard exceptions.

## Setup
Include the header in your C++ SQLite extension project:
```cpp
#include "include/sqlite3_statement.hpp"
```

---

## Examples of Usage

### 1. Basic DDL / DML Execution (`execute()`)
```cpp
#include "sqlite3_statement.hpp"

void init_db(sqlite3* db) {
    SqliteStatement stmt(db, "CREATE TABLE users(id INT PRIMARY KEY, name TEXT, score REAL);");
    if (stmt.execute() == SQLITE_DONE) {
        // Table created successfully!
    }
}
```

### 2. Parameterized Insertion & Step Reuse
```cpp
void insert_user(sqlite3* db, int id, const char* name, double score) {
    SqliteStatement stmt(db, "INSERT INTO users VALUES (?, ?, ?);");
    
    // Bind by 1-based index
    stmt.bind(1, id);
    stmt.bind(2, name);
    stmt.bind(3, score);
    
    // Execute and auto-reset for next usage
    stmt.execute();
}
```

### 3. Named Parameter Binding
```cpp
void update_score(sqlite3* db, int user_id, double new_score) {
    SqliteStatement stmt(db, "UPDATE users SET score = :score WHERE id = :id;");
    
    stmt.bind(":score", new_score);
    stmt.bind(":id", user_id);
    
    stmt.execute();
}
```

### 4. Query Iteration & Zero-Allocation Views
```cpp
void print_high_scorers(sqlite3* db, double min_score) {
    SqliteStatement stmt(db, "SELECT id, name, score FROM users WHERE score >= ? ORDER BY score DESC;");
    stmt.bind(1, min_score);
    
    while (stmt.next()) {
        int id = stmt.column_int(0);
        
        // Zero heap allocation: extracts directly as a string view!
        SqliteStringView name = stmt.column_string_view(1);
        double score = stmt.column_double(2);
        
        // Use the extracted data...
    }
}
```

### 5. Seamless Synergy with Value Keys & Polymorphic Variants
```cpp
void store_column_in_map(sqlite3* db, std::map<SqliteValueOwned, int, std::less<>>& my_map) {
    SqliteStatement stmt(db, "SELECT payload FROM dynamic_data;");
    
    while (stmt.next()) {
        // Extract directly into an owned polymorphic key (SBO for ints/floats, dup for text/blob)
        SqliteValueOwned key = stmt.column_value_owned(0);
        my_map.emplace(std::move(key), 1);
    }
}
```

---

## API Reference

### Lifecycle & Handle Management
| Method | Return Type | Description |
| :--- | :--- | :--- |
| `SqliteStatement()` | Constructor | Constructs an empty, unprepared statement. |
| `SqliteStatement(db, sql, len = -1)` | Constructor | Constructs and prepares the SQL statement. |
| `prepare(db, sql, len = -1)` | `int` | Prepares a new SQL statement, finalizing any existing handle. |
| `finalize()` | `int` | Explicitly finalizes the statement and clears the handle. |
| `release()` | `sqlite3_stmt*` | Relinquishes ownership of the raw handle without finalizing. |
| `get()` | `sqlite3_stmt*` | Returns the raw `sqlite3_stmt*` handle. |
| `explicit operator bool()` | `bool` | Returns `true` if the internal statement handle is non-null. |

### Stepping & Control
| Method | Return Type | Description |
| :--- | :--- | :--- |
| `step()` | `int` | Advances the statement by one step (`SQLITE_ROW`, `SQLITE_DONE`, error). |
| `next()` | `bool` | Returns `true` on `SQLITE_ROW`, `false` on `SQLITE_DONE` or error. |
| `reset()` | `int` | Resets the statement for re-execution. |
| `clear_bindings()` | `int` | Clears all bound parameters to NULL. |
| `execute()` | `int` | Steps once to completion and automatically resets the statement. |

### Parameter Binding
| Method | Description |
| :--- | :--- |
| `bind(col, int / int64 / double)` | Binds primitive numeric values (1-based index). |
| `bind(col, const char*, len = -1)` | Binds UTF-8 text. |
| `bind(col, SqliteStringView / SqliteStringOwned)` | Binds string wrappers directly. |
| `bind(col, void* blob, len)` | Binds raw binary blob data. |
| `bind(col, SqliteBlobView / SqliteBlobOwned)` | Binds blob wrappers directly. |
| `bind(col, SqliteValueView / SqliteValueOwned)` | Binds polymorphic value wrappers. |
| `bind_null(col)` | Binds SQL NULL to parameter. |
| `bind(":name", val)` | Overload for all above types resolving parameter name to index. |

### Column Extractions
| Method | Return Type | Description |
| :--- | :--- | :--- |
| `column_count()` | `int` | Returns number of columns in the result set. |
| `column_type(col)` | `int` | Returns SQLite datatype code for column (0-based). |
| `column_name(col)` | `const char*` | Returns column name string. |
| `column_int(col)` | `int` | Returns 32-bit integer value. |
| `column_int64(col)` | `sqlite3_int64` | Returns 64-bit integer value. |
| `column_double(col)` | `double` | Returns 64-bit floating point value. |
| `column_text(col)` | `const char*` | Returns UTF-8 text pointer. |
| `column_blob(col)` | `const void*` | Returns raw binary blob pointer. |
| `column_bytes(col)` | `int` | Returns byte length of text or blob column. |
| `column_string_view(col)` | `SqliteStringView` | Zero-allocation text view wrapper. |
| `column_blob_view(col)` | `SqliteBlobView` | Zero-allocation blob view wrapper. |
| `column_value_view(col)` | `SqliteValueView` | Zero-allocation transient polymorphic value wrapper. |
| `column_value_owned(col)` | `SqliteValueOwned` | Memory-managed polymorphic copy. |
