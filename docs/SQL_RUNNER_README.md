# Zero-STD Interactive SQL Runner & Markdown Snapshot Validator (`sqlite3_sql_runner.hpp`)

`sqlite3_sql_runner.hpp` provides a zero-dependency, freestanding C++ test harness and interactive SQL script runner for `sqlite-ext-core`. It partitions SQL test files into cell-based execution blocks (`-- %% <Title>`), executes statements sequentially via RAII `SqliteStatement` and `SqliteDatabaseView`, pretty-prints aligned ASCII tables, and automatically validates query results against companion Markdown snapshot tables (`-- @snapshot`).

---

## Key Capabilities

1. **Zero-STD / Freestanding C++ Runtime**:
   - Zero usage of `<vector>`, `<string>`, `<map>`, `<iostream>`, or C++ runtime overhead.
   - 100% compatible with `-nostdlib++ -fno-exceptions -fno-rtti`.
   - Built on Small Buffer Optimized `SqliteValueVec<8>` (up to 8 columns stored inline on stack; dynamically spills to heap only when exceeding 8 columns).
   - All dynamic memory is tracked via `SqliteAllocator<T>` (`sqlite3_malloc64` / `sqlite3_free`).
   - String formatting routes through SQLite's native `sqlite3_snprintf`.

2. **Interactive Cell Execution Protocol (`-- %% <Title>`)**:
   - Partitions SQL scripts into logical execution cells (similar to VSCode/Jupyter cells).
   - Generates formatted ASCII banners displaying cell indices and descriptive titles.
   - Supports multi-statement batches (DDL, DML, queries) within a single cell.

3. **Mandatory Markdown Snapshot Verification (`-- @snapshot`)**:
   - Enforces that row-returning queries (`SELECT`, `INSERT ... RETURNING`) are validated against companion Markdown tables.
   - Automatically validates column count, row count, and typed cell values (`NULL`, `INTEGER`, `FLOAT`, `TEXT`, `BLOB`).
   - Supports empty-result assertion (header + divider only) for queries returning 0 rows.
   - Supports explicit validation bypass for non-deterministic queries (`-- @snapshot: skip`).

4. **Seamless 100% SQLite CLI Compatibility**:
   - All cell headers and snapshot directives are standard SQL comments (`--`).
   - Any test script runnable by `SqliteSqlRunner` can be piped directly into the standard SQLite CLI: `sqlite3 < test.sql`.

5. **Native RAII Database & Statement Interoperability**:
   - Seamlessly accepts `SqliteDatabaseView`, `SqliteDatabaseOwned`, or raw `sqlite3*`.
   - Uses `SqliteStatement` for deterministic statement finalization and parameter extraction.

---

## Anatomy of a Test Script (`.sql`)

```sql
-- %% 01. Schema Setup & Seed Data
CREATE TABLE users(
    id INT PRIMARY KEY,
    name TEXT NOT NULL,
    score REAL,
    avatar BLOB
);

INSERT INTO users VALUES(1, 'Alice', 95.5, X'DEADBEEF');
INSERT INTO users VALUES(2, 'Bob', 88.0, NULL);

-- %% 02. Query Verification
SELECT id, name, score FROM users ORDER BY id;
-- @snapshot
-- | id | name  | score |
-- |:---|:------|:------|
-- | 1  | Alice | 95.5  |
-- | 2  | Bob   | 88.0  |

-- %% 03. Empty Result Assertion
SELECT id, name FROM users WHERE id = 999;
-- @snapshot
-- | id | name |
-- |:---|:-----|

-- %% 04. Non-Deterministic / Dynamic Queries
SELECT random() AS rnd;
-- @snapshot: skip
```

---

## Usage Guide

### 1. In-Memory String Execution (`run_string`)

```cpp
#include "sqlite3_sql_runner.hpp"
#include "sqlite3_db.hpp"

using namespace sqlite_ext;

void run_in_memory_suite() {
    SqliteDatabaseOwned db = SqliteDatabaseOwned::open_memory();

    const char* kScript = 
        "-- %% 01. Create & Query Table\n"
        "CREATE TABLE items(sku TEXT PRIMARY KEY, qty INT);\n"
        "INSERT INTO items VALUES('A1', 10), ('B2', 20);\n"
        "\n"
        "SELECT sku, qty FROM items ORDER BY sku;\n"
        "-- @snapshot\n"
        "-- | sku | qty |\n"
        "-- |:---|:---|\n"
        "-- | A1  | 10  |\n"
        "-- | B2  | 20  |\n";

    bool success = SqliteSqlRunner::run_string(db, kScript, "Items Test Suite");
    if (!success) {
        // Handle failure
    }
}
```

### 2. File-Based Script Execution (`run_file` / `sqlite_run_file`)

```cpp
#include "sqlite3_sql_runner.hpp"
#include "sqlite3_db.hpp"

using namespace sqlite_ext;

void run_file_suite() {
    SqliteDatabaseOwned db = SqliteDatabaseOwned::open_memory();

    // Automatically attempts direct path, "docs/<filepath>", and "../../docs/<filepath>"
    bool success = SqliteSqlRunner::run_file(db, "tests/scripts/test_demo.sql");
    // Or via convenience free function / macro:
    // bool success = sqlite_run_file(db, "tests/scripts/test_demo.sql");
    // bool success = SQLITE_RUN_SQL_FILE(db, "tests/scripts/test_demo.sql");
    if (!success) {
        // Handle failure
    }
}
```

### 3. Turnkey Extension Test Executable (`SQLITE_RUN_SQL_EXAMPLE_MAIN`)

For extension testing, `SQLITE_RUN_SQL_EXAMPLE_MAIN` generates a complete, zero-boilerplate `main()` function that opens an in-memory database, registers your extension via an initialization callback, executes the target `.sql` test script, and returns exit code `0` on success or `1` on failure:

```cpp
#include "sqlite3_sql_runner.hpp"
#include "my_extension.hpp"

// Extension registration callback
static int init_extension(sqlite3* db) {
    return sqlite3_my_ext_init(db, nullptr, nullptr);
}

// Generates complete main() function running docs/MY_EXTENSION_EXAMPLE.sql
SQLITE_RUN_SQL_EXAMPLE_MAIN("docs/MY_EXTENSION_EXAMPLE.sql", init_extension)
```

### 4. Standalone SQL Test Executable (`SQLITE_RUN_SQL_FILE_MAIN`)

For standard SQL script execution without custom C extension callbacks:

```cpp
#include "sqlite3_sql_runner.hpp"

// Generates complete main() function running docs/SCHEMA_TEST.sql
SQLITE_RUN_SQL_FILE_MAIN("docs/SCHEMA_TEST.sql")
```

---

## CLI Output Example

When executed, `SqliteSqlRunner` generates clean, formatted terminal output:

```text
================================================================================
       SQLite Script Runner: Items Test Suite
================================================================================

--------------------------------------------------------------------------------
[Cell 01]  01. Create & Query Table
--------------------------------------------------------------------------------
SQL > CREATE TABLE items(sku TEXT PRIMARY KEY, qty INT);
   --> OK

SQL > INSERT INTO items VALUES('A1', 10), ('B2', 20);
   --> OK (2 row(s) affected)

SQL > SELECT sku, qty FROM items ORDER BY sku;
+--------------------+--------------------+
| sku                | qty                |
+--------------------+--------------------+
| A1                 | 10                 |
| B2                 | 20                 |
+--------------------+--------------------+
   --> 2 row(s) returned.
   --> [SNAPSHOT PASS] Output matches expected Markdown table snapshot (100% verified).

================================================================================
 SUCCESS: All SQL example scenarios executed and verified (1 snapshots matched)!
================================================================================
```

---

## API Reference

### `SqliteSqlRunner`

| Static Method | Description |
|---|---|
| `run_string(SqliteDatabaseView db, const char* script, const char* title = "SQL Script", bool require_snapshots = true)` | Executes cell-partitioned SQL script from an in-memory string buffer with snapshot validation. |
| `run_file(SqliteDatabaseView db, const char* filepath, bool require_snapshots = true)` | Reads and executes a `.sql` script file from disk with automatic search fallback resolution. |
| `run_file_with_init(const char* filepath, InitFn&& init_fn, bool require_snapshots = true)` | Opens an in-memory DB, invokes `init_fn(sqlite3*)`, and runs a `.sql` file. |
| `run_string_with_init(const char* script, const char* title, InitFn&& init_fn, bool require_snapshots = true)` | Opens an in-memory DB, invokes `init_fn(sqlite3*)`, and runs an in-memory script. |
| `parse_snapshot_block(const char* text, SqlTableBuffer& out_snap)` | Parses a companion Markdown snapshot table block from SQL comment stream. |
| `parse_cell_value(const char* str)` | Parses a string token into a typed `SqliteValueOwned` (`NULL`, `INTEGER`, `FLOAT`, or `TEXT`). |
| `format_value(const SqliteValueOwned& val, char* buf, size_t buf_size)` | Formats an individual SQLite value into display string. |
| `is_table_divider(const char* line)` | Detects Markdown table separator lines (`|---|:---|`). |
| `trim_whitespace(char* str)` | In-place trimming of leading and trailing whitespace. |

### Convenience Free Functions & Macros

| Function / Macro | Description |
|---|---|
| `sqlite_run_file(db, filepath, require_snapshots = true)` | Free function executing a `.sql` file against a database view. |
| `sqlite_run_string(db, script, title = "SQL Script", require_snapshots = true)` | Free function executing a SQL script string against a database view. |
| `SQLITE_RUN_SQL_FILE(db, filepath)` | Macro wrapping `SqliteSqlRunner::run_file(db, filepath)`. |
| `SQLITE_RUN_SQL_FILE_EX(db, filepath, require_snapshots)` | Macro wrapping `SqliteSqlRunner::run_file` with explicit snapshot flag. |
| `SQLITE_RUN_SQL_STRING(db, script, title)` | Macro wrapping `SqliteSqlRunner::run_string(db, script, title)`. |
| `SQLITE_RUN_SQL_FILE_MAIN(filepath)` | Generates a complete standalone `main()` function running `filepath` against an in-memory DB. |
| `SQLITE_RUN_SQL_EXAMPLE_MAIN(filepath, init_fn)` | Generates a complete standalone `main()` function initializing an extension via `init_fn` and running `filepath`. |

### `SqlTableBuffer`

Container holding dynamic rows of `SqliteValueVec<8>`.

| Member | Type | Description |
|---|---|---|
| `rows` | `SqliteValueVec<8>*` | Contiguous dynamic array of SBO row vectors. |
| `count` | `int` | Number of rows currently populated. |
| `capacity` | `int` | Allocated row capacity. |
| `is_present` | `bool` | `true` if a snapshot block was detected. |
| `skip_validation` | `bool` | `true` if `-- @snapshot: skip` was specified. |
| `add_row(SqliteValueVec<8>&& row)` | `void` | Appends a row vector, dynamically expanding capacity if needed. |
| `reset()` | `void` | Releases memory and resets counters. |

