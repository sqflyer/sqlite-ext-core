# Object-Oriented Virtual Tables (`sqlite3_vtab.hpp` & `sqlite3_vtab_arg.hpp`)

A zero-allocation, type-safe C++17 object-oriented framework for developing high-performance SQLite Virtual Tables, Table-Valued Functions (TVFs), custom indexing engines, and declarative argument parsers without dealing with the nightmare of raw C pointer casting and `sqlite3_module` function pointer routing.

> **Related References**:
> - [Virtual Table Architecture (`docs/VTAB_ARCHITECTURE.md`)](VTAB_ARCHITECTURE.md)
> - [Virtual Table Argument Parser Guide (`docs/VTAB_ARG_README.md`)](VTAB_ARG_README.md)
> - [Virtual Table Argument Parser Architecture (`docs/VTAB_ARG_ARCHITECTURE.md`)](VTAB_ARG_ARCHITECTURE.md)

---

## Core Features

1. **Zero-Overhead Polymorphic Routing**: C++ virtual method dispatches are automatically routed from SQLite's C-API directly into your class instances via standard-layout wrappers.
2. **Object-Oriented Lifecycle Management**: Destroying a table automatically deletes associated cursors and frees allocated resources cleanly.
3. **`-nostdlib++` Compliant Memory Model**: Fully integrates with `sqlite3_allocator.hpp` (`sqlite_new` and `sqlite_delete`), routing all heap allocations through SQLite's native memory arena (`sqlite3_malloc64` / `sqlite3_free`).
4. **Compile-Time Feature Flags (`VTabOptions`)**: Configure module capabilities (`ReadOnly`, `Writable`, `Eponymous`, `Savepoint`, `HasShadow`, `Renameable`, `Findable`) via template flags with compile-time dead-code elimination.
5. **Integrated Argument & Schema Parser (`sqlite3_vtab_arg.hpp`)**: Zero-allocation batch parsing of `CREATE VIRTUAL TABLE` arguments, official SQLite type affinities, composite primary keys, hidden columns, rowid aliases, generated expressions, and automatic clean DDL synthesis.
6. **Unified Error Message Propagation**: Return human-readable error messages to SQLite callers by implementing `get_error_message()` on `SqliteVTable`.
7. **Direct Shared State Injection**: Seamlessly share per-connection state structs across Virtual Tables, Scalar UDFs, and Aggregates with $O(1)$ single-instruction extraction.

---

## Usage Guide

To implement a Virtual Table, you define two cooperating classes:
1. **`SqliteVTabCursor`**: Handles row-level iteration, filtering, and column extraction.
2. **`SqliteVTable`**: Handles table lifecycle, schema declaration, parameter parsing, and cursor instantiation.

---

### 1. Define the Cursor (`SqliteVTabCursor`)

The cursor handles scanning and row navigation. Inherit from `SqliteVTabCursor`:

```cpp
#include "sqlite3_vtab.hpp"
#include "sqlite3_allocator.hpp"

class MySeriesCursor : public SqliteVTabCursor {
private:
    int m_current = 0;
    int m_max = 100;

public:
    int filter(int idxNum, const char* idxStr, SqliteUdfArgs args) override {
        // Handle WHERE clause constraints passed by bestIndex()
        m_current = 1;
        if (args.size() > 0 && !args[0].is_null()) {
            m_max = args[0].as_int();
        }
        return SQLITE_OK;
    }
    
    int next() override { 
        m_current++; 
        return SQLITE_OK; 
    }
    
    bool eof() override { 
        return m_current > m_max; 
    }
    
    int column(SqliteContext& ctx, int N) override {
        // N represents the 0-based column index
        if (N == 0) {
            ctx.result_int(m_current);
        }
        return SQLITE_OK;
    }
    
    int rowid(sqlite3_int64& pRowid) override {
        pRowid = m_current;
        return SQLITE_OK;
    }
};
```

---

### 2. Define the Table with Argument Parsing (`SqliteVTable` & `SqliteVTabArgs`)

Use `SqliteVTabArgs` and `SqliteVTabParamSchema` inside your static `connect()` factory method to parse engine parameters, validate options, synthesize clean DDL, and instantiate your table:

```cpp
#include "sqlite3_vtab.hpp"
#include "sqlite3_vtab_arg.hpp"
#include "sqlite3_allocator.hpp"

class MySeriesTable : public SqliteVTable {
private:
    int    m_capacity;
    int    m_mode;
    double m_rate;

public:
    MySeriesTable(sqlite3* db, int cap, int mode, double rate)
        : SqliteVTable(db), m_capacity(cap), m_mode(mode), m_rate(rate) {}

    // Factory method required by SqliteVTabModule router
    static int connect(SqliteConnectArgs& args) {
        // 1. Wrap raw argv into batch argument parser
        SqliteVTabArgs vargs(args);

        // 2. Declare parameter schema with defaults
        static const char* const kModes[] = { "fast", "strict", "lenient" };
        int    capacity = 1024;
        int    mode_idx = 0; // Default: "fast"
        double rate     = 1.0;

        SqliteVTabParamSchema schema;
        schema.bind_int("capacity", &capacity)
              .bind_enum("mode", kModes, 3, &mode_idx)
              .bind_double("rate", &rate);

        // 3. Validate arguments & reject unknown parameters with custom error
        SqliteStringView unknown_key;
        if (schema.validate(vargs, &unknown_key) > 0) {
            return args.set_error("unrecognized parameter '%.*s'",
                                  unknown_key.length(), unknown_key.data());
        }
        schema.parse(vargs);

        // 4. Synthesize clean DDL for sqlite3_declare_vtab (stripping engine parameters)
        char ddl[512] = {0};
        vargs.format_declare_vtab_sql(ddl, sizeof(ddl));
        int rc = sqlite3_declare_vtab(args.db(), ddl);
        if (rc != SQLITE_OK) return rc;

        // 5. Instantiate table instance using SQLite allocator
        args.set_instance(sqlite_new<MySeriesTable>(args.db(), capacity, mode_idx, rate));
        return SQLITE_OK;
    }

    static int create(SqliteConnectArgs& args) {
        return connect(args);
    }

    int bestIndex(SqliteIndexInfo& info) override {
        info.set_estimated_cost(10.0);
        return SQLITE_OK;
    }

    SqliteVTabCursor* open() override {
        return sqlite_new<MySeriesCursor>();
    }
};
```

---

### 3. Register the Module

To register a **Read-Only** virtual table:
```cpp
SqliteVTab::define<MySeriesTable>(db, "my_series");
// Or via master facade: SqliteExt::define_vtab<MySeriesTable>(db, "my_series");
```

---

## Writeable Virtual Tables

To register a **Writeable** virtual table, specify `VTabOptions::Writable` during module registration:

```cpp
SqliteVTab::define<MySeriesTable, VTabOptions::Writable>(db, "my_series");
```

Override `update()`, `begin()`, `sync()`, `commit()`, and `rollback()` on your `SqliteVTable` subclass:

```cpp
class MyWriteableTable : public SqliteVTable {
private:
    sqlite3_int64 m_max_rowid = 0;

public:
    MyWriteableTable(sqlite3* db) : SqliteVTable(db) {}

    int update(SqliteUdfArgs args, sqlite3_int64* pRowid) override {
        if (args.size() == 1) {
            // DELETE: args[0] is the rowid to delete
            sqlite3_int64 old_rowid = args[0].as_int64();
            // Delete row from internal storage...
            return SQLITE_OK;

        } else if (args[0].type() == SQLITE_NULL) {
            // INSERT: args[1] is new rowid (or NULL), args[2..] are column values
            sqlite3_int64 new_rowid = (args[1].type() == SQLITE_NULL) 
                                      ? ++m_max_rowid 
                                      : args[1].as_int64();
            int new_val = args[2].as_int();
            // Insert row into internal storage...

            *pRowid = new_rowid;
            return SQLITE_OK;

        } else {
            // UPDATE: args[0] is old rowid, args[1] is new rowid, args[2..] are columns
            sqlite3_int64 old_rowid = args[0].as_int64();
            sqlite3_int64 new_rowid = args[1].as_int64();
            int updated_val = args[2].as_int();
            // Update row in internal storage...

            return SQLITE_OK;
        }
    }

    int begin() override    { return SQLITE_OK; }
    int sync() override     { return SQLITE_OK; }
    int commit() override   { return SQLITE_OK; }
    int rollback() override { return SQLITE_OK; }
};
```

---

## Transactions and Savepoints (`VTabOptions::Savepoint`)

To participate in SQLite's nested transactions and savepoints (`SAVEPOINT`, `RELEASE`, `ROLLBACK TO`):

```cpp
class MyTransactionalTable : public SqliteVTable {
public:
    MyTransactionalTable(sqlite3* db) : SqliteVTable(db) {}

    int savepoint(int iSavepoint) override {
        // Snapshot internal storage state...
        return SQLITE_OK;
    }

    int release(int iSavepoint) override {
        // Commit and discard snapshot...
        return SQLITE_OK;
    }

    int rollbackTo(int iSavepoint) override {
        // Revert internal storage to snapshot...
        return SQLITE_OK;
    }
};

// Registration:
SqliteVTab::define<MyTransactionalTable, VTabOptions::Savepoint>(db, "my_tx_table");
```

---

## Shadow Table Protection (`VTabOptions::HasShadow`)

If your virtual table uses underlying SQLite shadow tables (like FTS5 does with `%_data` and `%_idx`), protect them against malicious SQL injection in defensive mode (`SQLITE_DBCONFIG_DEFENSIVE`):

```cpp
class MyShadowTable : public SqliteVTable {
public:
    MyShadowTable(sqlite3* db) : SqliteVTable(db) {}

    static int shadowName(const char* zName) {
        // Return 1 if this table is owned by your virtual table engine
        return (strstr(zName, "_internal_data") != nullptr) ? 1 : 0;
    }
};

// Registration:
SqliteVTab::define<MyShadowTable, VTabOptions::HasShadow>(db, "my_shadow_table");
```

---

## Renaming Virtual Tables (`VTabOptions::Renameable`)

Handle `ALTER TABLE ... RENAME TO ...` seamlessly:

```cpp
class MyRenameableTable : public SqliteVTable {
public:
    MyRenameableTable(sqlite3* db) : SqliteVTable(db) {}

    int rename(const char* zNewName) override {
        // Update internal references to the new table name...
        return SQLITE_OK;
    }
};

// Registration:
SqliteVTab::define<MyRenameableTable, VTabOptions::Renameable>(db, "my_renameable_table");
```

---

## Custom Error Message Propagation (`get_error_message()`)

When a virtual table method returns an error code (`SQLITE_ERROR`, `SQLITE_CONSTRAINT`, etc.), SQLite checks `pVTab->zErrMsg` to display human-readable diagnostics.

Simply override `get_error_message()` on `SqliteVTable`. The router automatically formats and allocates the message with `sqlite3_mprintf`:

```cpp
class ValidatedTable : public SqliteVTable {
private:
    const char* m_last_error = nullptr;

public:
    ValidatedTable(sqlite3* db) : SqliteVTable(db) {}

    const char* get_error_message() const override {
        return m_last_error;
    }

    int update(SqliteUdfArgs args, sqlite3_int64* pRowid) override {
        if (args.size() > 2 && args[2].as_int() < 0) {
            m_last_error = "Constraint failed: 'value' must be positive";
            return SQLITE_CONSTRAINT;
        }
        m_last_error = nullptr;
        return SQLITE_OK;
    }
};
```

When an invalid query executes, SQLite reports:
`Runtime error: Constraint failed: 'value' must be positive`.

---

## Eponymous Virtual Tables (Table-Valued Functions)

For tables that can be queried directly without `CREATE VIRTUAL TABLE` statements:

```sql
SELECT * FROM generate_series(1, 100);
```

Register with `VTabOptions::Eponymous`:

```cpp
SqliteVTab::define<MySeriesTable, VTabOptions::Eponymous>(db, "generate_series");
```

---

## Stateful Virtual Tables (`SqliteVTab::define_with_state`)

Virtual tables can participate in shared, per-connection application state alongside Scalar UDFs and Aggregates:

```cpp
struct AppCacheState {
    int cache_hits;
    int active_sessions;
};

class StatefulTable : public SqliteVTable {
public:
    StatefulTable(sqlite3* db) : SqliteVTable(db) {}

    static int connect(SqliteConnectArgs& args) {
        int rc = sqlite3_declare_vtab(args.db(), "CREATE TABLE x(id INT, val INT)");
        if (rc == SQLITE_OK) {
            // Access shared connection state during connect/create:
            AppCacheState* state = args.state<AppCacheState>();
            args.set_instance(sqlite_new<StatefulTable>(args.db()));
        }
        return rc;
    }

    int column(SqliteContext& ctx, int N) override {
        // Access shared state in 1 CPU instruction during column extraction:
        AppCacheState* state = ctx.state<AppCacheState>();
        state->cache_hits++;
        ctx.result_int(state->cache_hits);
        return SQLITE_OK;
    }
};

// Registration:
SqliteVTab::define_with_state<AppCacheState, StatefulTable>(db, "my_cache_tbl");
```

---

## The `MATCH` Operator: `xFindFunction` vs `bestIndex`

For full-text search, vector search, or custom filtering syntax (`WHERE col MATCH 'term'`):

### Approach A: The Slow Way (`xFindFunction` Full Scan)
SQLite rewrites `col MATCH 'term'` to `match('term', col)` and invokes your scalar function for **every row** ($O(N)$ scans).

### Approach B: The Fast Way (`bestIndex` + `filter` Index Pushdown)
Intercept `SQLITE_INDEX_CONSTRAINT_MATCH` natively in `bestIndex()`, omit the scalar function evaluation, and pass the search term directly into `filter()` ($O(1)$ / $O(\log N)$ indexed lookups):

```cpp
int bestIndex(SqliteIndexInfo& info) override {
    for (int i = 0; i < info.num_constraints(); ++i) {
        if (info.constraint(i).op == SQLITE_INDEX_CONSTRAINT_MATCH && info.constraint(i).usable) {
            info.usage(i).argvIndex = 1; // Pass search term to filter() as args[0]
            info.usage(i).omit = 1;      // Skip scalar function evaluation
            info.set_idx_num(42);        // Plan identifier
            return SQLITE_OK;
        }
    }
    return SQLITE_OK;
}

int filter(int idxNum, const char* idxStr, SqliteUdfArgs args) override {
    if (idxNum == 42) {
        SqliteStringView search_term = args[0].as_text();
        // Lookup matching rows directly in inverted index or vector store...
    }
    return SQLITE_OK;
}
```

---

## Complete Argument Parser Guide

For complete details on argument slicing, column flags, composite primary keys, generated columns, and declarative schemas, refer to [`docs/VTAB_ARG_README.md`](VTAB_ARG_README.md).
