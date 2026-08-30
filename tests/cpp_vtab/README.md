# Virtual Table Subsystem Test Suite (`tests/cpp_vtab`)

Comprehensive test suite verifying the C++ Virtual Table framework (`sqlite3_vtab.hpp`), connection-local shared state integration (`SqliteExtState`), and zero-allocation argument & composite primary key parsing (`sqlite3_vtab_arg.hpp`).

---

## 1. Test Suite Matrix

| Binary | Source File | Standard | Covered Subsystem | Test Cases |
| :--- | :--- | :--- | :--- | :---: |
| `test_vtab` | `test_vtab.cpp` | C++11 | Core Virtual Table Lifecycle, MATCH, Rename, Shadow | 7 |
| `test_vtab_state` | `test_vtab_state.cpp` | C++11 | Stateful Virtual Tables, Mutex-Guarded State, Multi-DB Isolation | 6 |
| `test_vtab_arg` | `test_vtab_arg.cpp` | C++11 | Argument Parsing, Composite Primary Keys, Param Schema | 5 |
| **Total** | **3 binaries** | | | **18 test cases** |

---

## 2. Test Specifications & Breakdown

### A. Core Virtual Table Lifecycle (`test_vtab.cpp`)
1. **`SeriesTable`**: Eponymous-capable read-only series generator.
2. **`ComplexTable`**: Writable virtual table supporting `INSERT`, `UPDATE`, and `DELETE`.
3. **`xFindFunction` Overloading**: Virtual table-specific UDF overloading in `SELECT` and `WHERE` clauses.
4. **`MATCH` Operator Integration**: Custom query planner indexing routing `MATCH` constraints to `xFilter`.
5. **Transactions & Savepoints**: Nested transaction lifecycle management (`xBegin`, `xSync`, `xCommit`, `xRollback`, `xSavepoint`, `xRelease`, `xRollbackTo`).
6. **Shadow Table Protection**: Defending underlying storage via `xShadowName`.
7. **Table Renaming**: Schema alteration support via `xRename`.

### B. Stateful Virtual Tables (`test_vtab_state.cpp`)
1. **Shared State Streaming**: Streaming virtual table rows directly from mutex-guarded `SqliteExtState`.
2. **State Mutation via SQL**: Mutating shared state via `INSERT` into virtual table.
3. **Companion UDF Reflection**: UDF mutations dynamically reflected in virtual table reads.
4. **Multi-Connection Isolation**: Verifies that opening `db2` starts fresh state without crosstalk from `db1`.

### C. Virtual Table Argument & Composite PK Parser (`test_vtab_arg.cpp`)
1. **`SqliteVTabParam` Typed Accessors**: Parses integers (`as_int`, `as_long`), floating-point (`as_double`), sizes (`as_size`), strings (`as_str`), and case-insensitive booleans (`as_bool`).
2. **`SqliteVTabColumn` Inspection**: Schema affinities (Integer, Text, Blob, Real, Numeric) and constraints (`NotNull`, `PrimaryKey`, `Unique`, `AutoIncr`, `Hidden`).
3. **`SqliteVTabConstraint` (Multi-Column PK)**: Composite primary keys (e.g. `PRIMARY KEY (user_id, device_id, timestamp)`), named constraints (`CONSTRAINT pk_custom PRIMARY KEY ...`), unique constraints, and checks.
4. **`SqliteVTabArgs` Batch Parsing**: Aggregated primary key inspection (`is_composite_primary_key()`, `is_primary_key_column()`, `primary_key_count()`).
5. **`SqliteVTabParamSchema`**: Fluent parameter declaration with case-insensitive enum validation (`bind_enum`).
6. **Virtual Table SQL Integration**: End-to-end SQLite query execution verifying argument and composite PK reflection.

---

## 3. Compilation & Execution Guide

### Windows MSVC (`cl.exe`)
```cmd
cd tests\cpp_vtab
make.bat clean
make.bat build
make.bat test
```

### MSYS2 / GCC / Clang
```bash
cd /home/dilipvamsi/works/repos/sqlite-ext-core/tests/cpp_vtab
make clean
make all
make test
```
