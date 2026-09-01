# Architectural Specification: Zero-STD SQL Runner & Snapshot Validator (`sqlite3_sql_runner.hpp`)

This document outlines the internal architecture, memory layout, parsing state machine, and snapshot verification engine of `sqlite3_sql_runner.hpp`.

---

## 1. Architectural Philosophy & Invariants

The `SqliteSqlRunner` framework is designed to provide high-performance, deterministic SQL test execution and result set assertion under the strict operational constraints of `sqlite-ext-core`:

```
+-------------------------------------------------------------------------+
|                  SqliteSqlRunner Execution Pipeline                     |
+-------------------------------------------------------------------------+
|  [SQL Script] --%% Cell Parsing --> [SqliteStatement] --step()--> Rows  |
|                                                                    |     |
|  [-- @snapshot] --Markdown Table--> [SqlTableBuffer] <---Compare---+    |
|                                            |                             |
|                                            v                             |
|                         [ASCII Formatted Output]                         |
+-------------------------------------------------------------------------+
```

### Invariants:
1. **Zero Standard C++ Runtime Overhead**:
   - Zero `<iostream>`, `<vector>`, `<string>`, `<map>`, or C++ runtime dependencies.
   - 100% compatible with `-nostdlib++ -fno-exceptions -fno-rtti`.
2. **Small Buffer Optimization (SBO)**:
   - Queries returning up to 8 columns allocate zero heap memory for row vectors (`SqliteValueVec<8>`).
   - Tables with > 8 columns seamlessly spill over to heap memory via `SqliteAllocator<T>`.
3. **Deterministic SQLite Memory Tracking**:
   - All heap allocations route through `sqlite3_malloc64` and `sqlite3_free`.
4. **100% CLI Pipeline Interchangeability**:
   - Every annotation (`-- %%`, `-- @snapshot`, `-- @snapshot: skip`) is a valid SQLite single-line comment. Standard command line tools (`sqlite3 < script.sql`) execute cleanly without modifications.

---

## 2. Memory Architecture & Data Layout

### 2.1 `SqlTableBuffer` Memory Model

`SqlTableBuffer` manages dynamic collections of rows:

```
SqlTableBuffer (32/64 bytes)
+---------------+---------------+---------------+---------------+------------------+
| rows*         | count         | capacity      | is_present    | skip_validation  |
| (pointer)     | (int)         | (int)         | (bool)        | (bool)           |
+-------+-------+---------------+---------------+---------------+------------------+
        |
        v
  Heap Array of SqliteValueVec<8>
  +-------------------------------------------------------------+
  | Row 0: [ Col 0 | Col 1 | Col 2 | ... | Col 7 ] (Inline SBO) |
  +-------------------------------------------------------------+
  | Row 1: [ Col 0 | Col 1 | Col 2 | ... | Col 7 ] (Inline SBO) |
  +-------------------------------------------------------------+
  | Row N: [ Col 0 | Col 1 | ... | Col 15 ] (Spilled Heap Array)|
  +-------------------------------------------------------------+
```

When row count exceeds `capacity`, `add_row` doubles the capacity ($8 \rightarrow 16 \rightarrow 32 \dots$) and transfers ownership of each `SqliteValueVec<8>` using `sqlite_move`.

---

## 3. Parsing State Machine & Cell Pipeline

The script execution engine processes the input script in two discrete phases:

### Phase A: Cell Partitioning
1. Scans for cell boundary tokens (`-- %% <Title>` or `--%%<Title>`).
2. Extracts and trims the optional cell title from the header line.
3. Isolates the SQL body between the current boundary and the next `-- %%` marker.

```
Script Content:
[-- %% Cell 1 Title\n]  --->  Title: "Cell 1 Title"
[CREATE TABLE ...;\n ]  --->  Statement 1 (DDL)
[INSERT INTO ...;\n  ]  --->  Statement 2 (DML)
[-- %% Cell 2 Title\n]  --->  Next Cell Boundary
```

### Phase B: Statement Iteration & Snapshot Extraction
Within each cell body, the runner sequentially consumes SQL statements:
1. Skips leading whitespace and comment lines (`-- ...`).
2. Compiles statement via `sqlite3_prepare_v2(db, pSql, -1, &raw_stmt, &pTail)`.
3. Wraps `raw_stmt` into RAII `SqliteStatement`.
4. Inspects trailing buffer (`pTail`) for an immediately adjacent companion `-- @snapshot` block:
   - If `-- @snapshot: skip` is encountered, snapshot verification is bypassed.
   - If `-- @snapshot` is encountered, lines are parsed as Markdown table rows (`-- | col1 | col2 |`).
   - If non-comment SQL is encountered before `-- @snapshot`, snapshot parsing terminates to prevent cross-statement pollution.

---

## 4. Snapshot Table Parser & Type Inference

`parse_snapshot_block` extracts column headers, skips alignment dividers (`|:---|:---:|---:|`), and converts table cells into typed `SqliteValueOwned` instances:

```
Markdown Line: "-- | 100 | Alice | 95.5 | NULL |"
                   |      |       |      |
                   v      v       v      v
Value Type:      INT    TEXT    FLOAT   NULL
```

### Type Inference Rules (`parse_cell_value`):
- `"NULL"` or `"null"` $\rightarrow$ `SQLITE_NULL`
- Valid integer string matching `strtoll` $\rightarrow$ `SQLITE_INTEGER`
- Valid floating-point string matching `strtod` $\rightarrow$ `SQLITE_FLOAT`
- Default fallback $\rightarrow$ `SQLITE_TEXT`

---

## 5. Value Equivalence & Assertion Rules

Verification enforces strict equivalence across actual and expected result sets:

1. **Row Count Check**:
   $$\text{snapshot.count} == \text{actual\_results.count}$$
2. **Column Cardinality Check**:
   $$\text{check\_cols} = \min(\text{expected.size()}, \text{actual.size()})$$
3. **Typed Cell Equality**:
   Evaluates `expected[c] == actual[c]` using native `SqliteValueOwned` equality:
   - Integers: exact 64-bit comparison ($a == b$).
   - Floats: IEEE 754 float comparison.
   - Text: exact string comparison via `SqliteStringView`.
   - Blobs: exact byte sequence and size comparison via `SqliteBlobView`.
   - Nulls: type identity check (`is_null()`).

---

## 6. File Resolution Engine (`run_file`)

`SqliteSqlRunner::run_file` provides intelligent search fallback for running test scripts across varied directory layouts (e.g. from workspace root, from `tests/`, or from `tests/cpp_sql_runner/`):

1. **Direct Path**: Tries `filepath` directly.
2. **Docs Relative**: Tries `docs/<filepath>`.
3. **Nested Docs Relative**: Tries `../../docs/<filepath>`.

---

## 7. Turnkey Test Harness & Runner Macros

To eliminate boilerplate across extension repositories, the runner provides standalone macro generators:

```
                  +-----------------------------------+
                  |  SQLITE_RUN_SQL_EXAMPLE_MAIN(...) |
                  +-----------------+-----------------+
                                    |
            +-----------------------v-----------------------+
            | 1. SqliteDatabaseOwned::open_memory()         |
            | 2. Invoke init_fn(db.get()) -> SQLITE_OK     |
            | 3. SqliteSqlRunner::run_file(db, filepath)    |
            | 4. Return exit code (0 = PASS, 1 = FAIL)      |
            +-----------------------------------------------+
```

- **`SQLITE_RUN_SQL_EXAMPLE_MAIN(filepath, init_fn)`**: Synthesizes a freestanding `main()` that opens an in-memory database, runs extension initialization, executes the test script, and returns exit code 0 or 1.
- **`SQLITE_RUN_SQL_FILE_MAIN(filepath)`**: Synthesizes a freestanding `main()` for standard SQL script execution without custom C callbacks.
- **`run_file_with_init(filepath, init_fn)` / `run_string_with_init(script, title, init_fn)`**: Template methods enabling programmatic execution with custom lambdas or function pointers.

---

## 8. Verification Matrix

| Component | Test File | Verification Scope |
|---|---|---|
| Buffer Growth & SBO | `test_sql_runner.cpp` | Buffer expansion ($0 \rightarrow 8 \rightarrow 16 \rightarrow 32$), SBO stack-to-heap spill (> 8 cols), Move semantics |
| Parser Edge Cases | `test_sql_runner.cpp` | Whitespace trimming, Table dividers (`:---|---`), UTF-8 international characters, Compact comments (`--%%`, `--@snapshot`) |
| Execution Engine | `test_sql_runner.cpp` | DDL/DML changes tracking, SELECT row formatting, Missing snapshot enforcement, Value mismatches, Error propagation |
| File I/O Engine | `test_sql_runner.cpp` | Valid `.sql` files, Empty files, Non-existent file error handling, Free functions (`sqlite_run_file`, `sqlite_run_string`) |
| Turnkey Macro Harness | `test_example_main.cpp` | Standalone executable generated via `SQLITE_RUN_SQL_EXAMPLE_MAIN` with extension initialization |
| Standalone File Harness | `test_file_main.cpp` | Standalone executable generated via `SQLITE_RUN_SQL_FILE_MAIN` |

