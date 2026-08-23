# Object-Oriented Virtual Tables (`sqlite3_vtab.hpp`)

This header provides a clean, C++ inheritance-based framework for writing powerful SQLite Virtual Tables without dealing with the nightmare of raw C pointer casting and `sqlite3_module` function pointer routing.

## Core Features
1. **Zero-Overhead Routing**: C++ virtual method dispatches are automatically routed from SQLite's C-API directly into your class instances.
2. **Object-Oriented Lifecycle**: Destroying a table automatically deletes all associated cursors and internal memory allocations.
3. **`-nostdlib++` Compliant Allocation**: Seamlessly integrates with `sqlite3_allocator.hpp` to intercept memory allocation natively without invoking standard library heap routines.
4. **Compile-Time Feature Flags**: You can explicitly register a module as read-only or writeable by flipping a single C++ template parameter.

## Usage Guide

To implement a Virtual Table, you need to implement two classes: a **Cursor** (to iterate over rows) and a **Table** (to spawn cursors and handle table-level constraints).

### 1. Define the Cursor
The cursor handles iterating through the data. It must inherit from `SqliteVTabCursor`.

```cpp
#include "sqlite3_vtab.hpp"
#include "sqlite3_allocator.hpp" // Required for sqlite_new

class MySeriesCursor : public SqliteVTabCursor {
private:
    int m_current = 0;
    int m_max = 100;
public:
    int filter(int idxNum, const char* idxStr, SqliteUdfArgs args) override {
        // Handle WHERE clause constraints passed by xBestIndex
        m_current = 1;
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
        // Use SqliteContext's zero-allocation helper to return data
        ctx.result_int(m_current);
        return SQLITE_OK;
    }
    
    int rowid(sqlite3_int64& pRowid) override {
        pRowid = m_current;
        return SQLITE_OK;
    }
};
```

### 2. Define the Table
The table parses schemas and spawns cursors. It must inherit from `SqliteVTable`. 

> **CRITICAL**: Because we compile with `-nostdlib++`, you **must** use `sqlite_new<T>()` instead of `new` when returning object instances.

```cpp
class MySeriesTable : public SqliteVTable {
public:
    MySeriesTable(sqlite3* db) : SqliteVTable(db) {}

    // Factory method required by the module router
    static int connect(SqliteConnectArgs& args) {
        int rc = sqlite3_declare_vtab(args.db(), "CREATE TABLE x(value INT)");
        if (rc == SQLITE_OK) {
            // MUST use sqlite_new in -nostdlib++ environments
            args.set_instance(sqlite_new<MySeriesTable>(args.db()));
        }
        return rc;
    }

    int bestIndex(SqliteIndexInfo& info) override {
        // Use SqliteIndexInfo helper to parse constraints
        info.set_estimated_cost(100.0);
        return SQLITE_OK;
    }

    SqliteVTabCursor* open() override {
        // MUST use sqlite_new in -nostdlib++ environments
        return sqlite_new<MySeriesCursor>();
    }
};
```

### 3. Register the Module
To register a **Read-Only** virtual table:
```cpp
SqliteVTab::define<MySeriesTable>(db, "my_series");
// Or via umbrella: SqliteExt::define_vtab<MySeriesTable>(db, "my_series");
```

---

## Writeable Virtual Tables

To register a **Writeable** virtual table, provide the `VTabOptions::Writable` template parameter during registration:

```cpp
SqliteVTab::define<MySeriesTable, VTabOptions::Writable>(db, "my_series");
```

By explicitly declaring it writeable, `SqliteVTabModule` will conditionally route `xUpdate`, `xBegin`, `xSync`, `xCommit`, and `xRollback` function pointers to your class instance. 

You must override the following methods on your `SqliteVTable` subclass:

```cpp
class MyWriteableTable : public SqliteVTable {
    sqlite3_int64 m_max_rowid = 100; // Track highest rowid for INSERTS

    // ... all other methods ...

    // Required for writeable tables
    int update(SqliteUdfArgs args, sqlite3_int64* pRowid) override {
        // SQLite passes different arguments depending on the operation:
        // DELETE: args.size() == 1, args[0] is the rowid to delete.
        // INSERT: args.size() > 1, args[0] is SQLITE_NULL. args[1] is the new rowid, args[2+] are the new column values.
        // UPDATE: args.size() > 1, args[0] is the old rowid. args[1] is the new rowid, args[2+] are the new column values.

        if (args.size() == 1) {
            // ==========================================
            // DELETE operation
            // ==========================================
            sqlite3_int64 old_rowid = args[0].as_int64();
            
            // TODO: Delete row with old_rowid from your internal storage
            
            return SQLITE_OK;
            
        } else if (args[0].type() == SQLITE_NULL) {
            // ==========================================
            // INSERT operation
            // ==========================================
            
            // Generate a new RowID if the user didn't explicitly specify one
            sqlite3_int64 new_rowid = (args[1].type() == SQLITE_NULL) 
                                      ? ++m_max_rowid 
                                      : args[1].as_int64();
                                      
            // Extract column values (assuming a single INT column 'value' as per schema)
            int new_col_val = (int)args[2].as_int64();
            
            // TODO: Insert row (new_rowid, new_col_val) into your internal storage
            
            *pRowid = new_rowid; // SQLite requires us to return the inserted rowid
            return SQLITE_OK;
            
        } else {
            // ==========================================
            // UPDATE operation
            // ==========================================
            sqlite3_int64 old_rowid = args[0].as_int64();
            sqlite3_int64 new_rowid = args[1].as_int64();
            
            // Extract updated column values
            int new_col_val = (int)args[2].as_int64();
            
            // TODO: Update row in your internal storage
            // Example logic:
            // if (old_rowid != new_rowid) delete_row(old_rowid);
            // insert_or_replace_row(new_rowid, new_col_val);
            
            return SQLITE_OK;
        }
    }

    // Optional transactions (return SQLITE_OK by default)
    int begin() override { return SQLITE_OK; }
    int sync() override { return SQLITE_OK; }
    int commit() override { return SQLITE_OK; }
    int rollback() override { return SQLITE_OK; }
};
```

---

## 3. Transactions and Savepoints (`VTabOptions::Savepoint`)

If you want your virtual table to participate in SQLite's nested transactions and savepoints, you must include `VTabOptions::Savepoint` in your registration options and implement the savepoint methods.

```cpp
class MyTransactionalTable : public SqliteVTable {
    // ...
    int savepoint(int iSavepoint) override {
        // e.g., create a snapshot of internal state
        return SQLITE_OK;
    }
    int release(int iSavepoint) override {
        // e.g., discard the snapshot
        return SQLITE_OK;
    }
    int rollbackTo(int iSavepoint) override {
        // e.g., revert internal state to the snapshot
        return SQLITE_OK;
    }
};

// Registration:
SqliteVTabModule<MyTransactionalTable, VTabOptions::Savepoint>::register_module(db, "my_tx_table");
```

---

## 4. Shadow Table Protection (`VTabOptions::HasShadow`)

If your virtual table stores data internally inside hidden SQLite tables (like FTS5 does with its `%_data` and `%_idx` tables), you must protect those "shadow tables" from being manually modified or dropped by malicious SQL injection. 

If a user enables `SQLITE_DBCONFIG_DEFENSIVE`, SQLite will call your `shadowName()` method whenever anyone tries to `CREATE` or `DROP` a table to ask if it belongs to you.

```cpp
class MyShadowTable : public SqliteVTable {
    // ...
    static int shadowName(const char* zName) {
        // SQLite passes the FULL table name being created (e.g. 'my_vtab_internal_data')
        // Return 1 if this is your shadow table. SQLite will BLOCK the creation!
        return strstr(zName, "_internal_data") != nullptr ? 1 : 0;
    }
};

// Registration:
SqliteVTabModule<MyShadowTable, VTabOptions::HasShadow>::register_module(db, "my_shadow_table");
```

---

SqliteVTab::define<MyTransactionalTable, VTabOptions::Savepoint>(db, "my_tx_table");
```

---

## 4. Shadow Tables (Defensive Mode)

To protect underlying storage tables with `xShadowName`:

```cpp
// Registration:
SqliteVTab::define<MyShadowTable, VTabOptions::HasShadow>(db, "my_shadow_table");
```

---

## 5. Renaming Virtual Tables (`xRename`)

To handle `ALTER TABLE RENAME TO`:

```cpp
// Registration:
SqliteVTab::define<MyRenameableTable, VTabOptions::Renameable>(db, "my_renameable_table");
```

---

## 6. Eponymous Virtual Tables (`VTabOptions::Eponymous`)

Sometimes you don't want the user to have to run `CREATE VIRTUAL TABLE x USING module`. You just want them to be able to instantly query a function that returns a table:

```sql
SELECT * FROM generate_series(1, 100);
```

These are called **Eponymous Virtual Tables**. You build them exactly like a normal read-only Virtual Table, but when registering them, provide the `VTabOptions::Eponymous` template option:

```cpp
// Register as Eponymous via compile-time option flag:
SqliteVTab::define<MySeriesTable, VTabOptions::Eponymous>(db, "generate_series");
// Or via umbrella: SqliteExt::define_vtab<MySeriesTable, VTabOptions::Eponymous>(db, "generate_series");
```

---

## 7. Stateful Virtual Tables (`SqliteVTab::define_with_state`)

Virtual tables can participate in shared per-connection state alongside Scalar UDFs and Aggregates using **`SqliteVTab::define_with_state`**:

```cpp
struct AppCacheState {
    int total_reads;
    int cache[10];
};

class StatefulTable : public SqliteVTable {
public:
    StatefulTable(sqlite3* db) : SqliteVTable(db) {}

    static int connect(SqliteConnectArgs& args) {
        int rc = sqlite3_declare_vtab(args.db(), "CREATE TABLE x(id INT, val INT)");
        if (rc == SQLITE_OK) {
            // Retrieve shared state during connect/create:
            AppCacheState* state = args.state<AppCacheState>();
            args.set_instance(sqlite_new<StatefulTable>(args.db()));
        }
        return rc;
    }
};

// Register stateful virtual table:
SqliteVTab::define_with_state<AppCacheState, StatefulTable>(db, "my_cache_tbl");
// Or via umbrella: SqliteExt::define_vtab_with_state<AppCacheState, StatefulTable>(db, "my_cache_tbl");
```

Inside cursor `column(SqliteContext& ctx, int N)`, state is resolved via **`ctx.state<AppCacheState>()`** in 1 CPU instruction.

---

## 8. The `MATCH` Operator: `xFindFunction` vs `bestIndex`

One of the most powerful features of virtual tables is the ability to implement custom search logic for keywords like `MATCH` (e.g. `WHERE column MATCH 'search_term'`). 

You have two architectural choices for implementing this:

### Approach A: The "Slow" Way: `xFindFunction` (Full Table Scan)
If the user executes this query:
```sql
SELECT * FROM my_table WHERE column MATCH 'search_term';
```

If you register a UDF via `findFunction()`, SQLite will silently rewrite the query internally to:
```sql
SELECT * FROM my_table WHERE match('search_term', column);
```
SQLite will then execute your virtual table by iterating over *every single row* and calling your UDF for each one.

```cpp
SqliteFunctionDef findFunction(int nArg, const char* zName) override {
    if (strcmp(zName, "match") == 0) {
        return SqliteFunctionDef::wrap<my_match_udf>(this); 
    }
    return {};
}

// Called N times for N rows in the table
static void my_match_udf(SqliteContext& ctx, SqliteUdfArgs args) {
    const char* search_term = args[0].text();
    // Manual evaluation...
    ctx.result_int(1); // Keep row
}
```
*Performance: O(N). If your table has 10 million rows, the UDF is executed 10 million times.*

### Approach B: The "Fast" Way: `bestIndex` + `filter` (Query Planner)

If the user executes the exact same query:
```sql
SELECT * FROM my_table WHERE column MATCH 'search_term';
```

If you intercept `SQLITE_INDEX_CONSTRAINT_MATCH` natively in the query planner, SQLite will **not** rewrite the query into a function, and it will **completely skip the UDF**. 

Instead, it passes the string `'search_term'` directly to `filter()`, allowing you to use an internal C++ data structure (like an inverted index or hash map) to instantly yield the matching rows.

```cpp
int bestIndex(SqliteIndexInfo& info) override {
    for (int i = 0; i < info.num_constraints(); ++i) {
        if (info.constraint(i).op == SQLITE_INDEX_CONSTRAINT_MATCH && info.constraint(i).usable) {
            info.usage(i).argvIndex = 1; // Pass 'search_term' to filter() as args[0]
            info.usage(i).omit = 1;      // Skip the UDF!
            info.set_idx_num(42);        // Signal filter()
            return SQLITE_OK;
        }
    }
    return SQLITE_OK;
}

// Called EXACTLY ONCE per query
int filter(int idxNum, const char* idxStr, SqliteUdfArgs args) override {
    if (idxNum == 42) {
        const char* search_term = args[0].text(); 
        
        // 1. Instantly lookup rows in a C++ std::unordered_map
        // 2. Set cursor limits to yield exactly those rows
    }
    return SQLITE_OK;
}
```
*Performance: O(1) or O(log N). This is exactly how FTS5 achieves blazing fast search.*
