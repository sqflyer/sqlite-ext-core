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

`parse_snapshot_block` extracts column headers, skips alignment dividers (`|:---|:---:|---:|`), and converts table cells into typed `SqliteValueOwned` instances via `SqliteSqlRunner::parse_cell_value` (which directly delegates to `SqliteValueOwned::from_literal`):

```
Markdown Line: "-- | 100 | Alice | 95.5 | true | X'DEADBEEF' | NULL |"
                   |     |       |      |      |             |
                   v     v       v      v      v             v
Value Type:      INT   TEXT    FLOAT   BOOL   BLOB          NULL
```

### 4.1 Type Inference State Machine (`SqliteValueOwned::from_literal`)

The parsing pipeline evaluates input string tokens (`SqliteStringView`) through the following deterministic stages:

```
+-------------------------------------------------------------------------------+
|                       from_literal Parsing Pipeline                           |
+-------------------------------------------------------------------------------+
| 1. Empty / "null" (case-insensitive)        ---> SQLITE_NULL                  |
| 2. "true"/"yes"/"on" / "false"/"no"/"off"   ---> SQLITE_INTEGER (BOOL sub)    |
| 3. X'...' / x'...' (even hex length)        ---> SQLITE_BLOB (SBO <=22B/Heap) |
| 4. 'text' / "text" (matching outer quotes)  ---> SQLITE_TEXT (quotes stripped)|
| 5. Signed Integer (is_num, dot=0, e=0)      ---> SQLITE_INTEGER (64-bit int)  |
| 6. Floating-point (is_num, dot=1 or e=1)    ---> SQLITE_FLOAT (double)        |
| 7. Fallback (unquoted text, malformed nums) ---> SQLITE_TEXT                  |
+-------------------------------------------------------------------------------+
```

### 4.2 Type Inference & Conversion Rules:

1. **NULL Recognition**:
   - `""` (empty string), `"null"`, `"NULL"`, `"Null"` $\rightarrow$ `SQLITE_NULL` (`type() == SQLITE_NULL`, `is_null() == true`).
   - Slices cutting `"null"` (e.g. `"nul"`) or prefixed identifiers (`"nullify"`) fall through to `SQLITE_TEXT`.

2. **Boolean Recognition**:
   - Case-insensitive true tokens: `"true"`, `"yes"`, `"on"` $\rightarrow$ `SQLITE_INTEGER` with `1LL`, tagged with `SQLITE_SUBTYPE_BOOL ('B' = 66)`.
   - Case-insensitive false tokens: `"false"`, `"no"`, `"off"` $\rightarrow$ `SQLITE_INTEGER` with `0LL`, tagged with `SQLITE_SUBTYPE_BOOL ('B' = 66)`.
   - Identifiers with boolean prefixes (`"true1"`, `"yesterday"`, `"offline"`) safely fall through to `SQLITE_TEXT`.

3. **SQLite Standard BLOB Literals (`X'...'` / `x'...'`)**:
   - Uppercase `X'...'` and lowercase `x'...'` containing an even count of hex digits are decoded into binary byte arrays.
   - **In-situ SBO Optimization**: $\le 22$ bytes (44 hex digits) are decoded directly into `InlineBufferRep` with **zero** heap allocations.
   - **Heap Spillover**: $> 22$ bytes allocate memory via `sqlite3_malloc64` and transfer into owned heap buffer.
   - Malformed BLOB literals (odd hex length like `X'123'`, invalid hex chars like `X'ZZ'`, unclosed quotes) safely fall through to `SQLITE_TEXT`.

4. **Quoted String Stripping (`'...'` and `"..."`)**:
   - Single-quoted (`'...'`) and double-quoted (`"..."`) string literals have their outer quotes stripped.
   - Empty quotes (`''`, `""`) produce 0-length `SQLITE_TEXT`.
   - Quoted numbers, booleans, and nulls (`'123'`, `"true"`, `'null'`) remain pure `SQLITE_TEXT` and are never coerced.
   - Unbalanced or mismatched quotes (`'hello"`, `"world'`) are preserved as raw unquoted `SQLITE_TEXT`.

5. **Integer Numbers**:
   - Valid signed integer tokens (`"0"`, `"+5"`, `"-42"`, `"9223372036854775807"`, `"-9223372036854775808"`, `"007"`) are parsed via `sscanf` on a slice-safe null-terminated buffer into 64-bit `sqlite3_int64` (`SQLITE_AFF_INTEGER`).

6. **Floating-Point Numbers**:
   - Decimal tokens with a single dot (`"3.1415"`, `"-0.005"`, `"42."`) or standard scientific notation (`"1.5e-3"`, `"+1E+6"`, `"-2.5E-3"`) are parsed into IEEE 754 `double` (`SQLITE_AFF_REAL`).

7. **Malformed Numbers & Plain Text Fallback**:
   - Multi-dot strings (`"1.2.3"`, `"192.168.1.1"`), multi-exponent strings (`"1e2e3"`), incomplete scientific exponents (`"1e"`, `"1e+"`), sign anomalies (`"++5"`, `"+"`), leading dot (`".5"`), and C-style hex/bin literals (`"0x10"`, `"0b101"`) fall through to `SQLITE_TEXT`.
   - SQL identifiers (`strict`, `user_id`), ISO-8601 timestamps (`2026-09-04T19:08:52Z`), JSON blocks (`{"id": 1}`), URLs, file paths, and punctuation symbols default to `SQLITE_TEXT`.

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

