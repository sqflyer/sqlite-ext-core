# SQLite Database C++ Wrappers (`sqlite3_db.hpp`)

This header provides a robust, zero-cost RAII (Resource Acquisition Is Initialization) wrapper for managing SQLite connection lifecycles and rapidly executing queries.

## 1. Owned vs View Connections

We provide two distinct classes to manage database connections:

### `SqliteDatabaseOwned`
Use this when you want to open a brand new database connection from disk or memory. It automatically calls `sqlite3_close_v2` when it goes out of scope.
```cpp
{
    SqliteDatabaseOwned db("my_app.sqlite");
    db.exec("CREATE TABLE users (id INT);");
} // <- Automatically closes here!
```

### `SqliteDatabaseView`
Use this when SQLite hands you a raw `sqlite3*` handle (e.g., inside an extension callback) and you want to use the C++ helper methods without actually taking ownership of the handle.
```cpp
void my_c_callback(sqlite3* raw_db) {
    SqliteDatabaseView db(raw_db);
    db.exec("INSERT INTO logs VALUES ('callback fired');");
} // <- Does NOT close the database here.
```

## 2. Fast Query Execution (`.exec`)

For static queries without parameters (like `CREATE TABLE`), use the `.exec()` shortcut on either a Database, Transaction, or Savepoint object. It safely wraps `sqlite3_exec`.

```cpp
SqliteDatabaseOwned db(":memory:");
db.exec("CREATE TABLE test (val INT);");

SqliteTransaction txn(db);
txn.exec("INSERT INTO test VALUES (1);");
txn.commit();
```

## 3. Parameterized Query Builders (`.prepare`)

Whenever you need to use parameters (e.g., `?`), use the `.prepare()` method. It instantly generates an RAII `SqliteStatement` object, allowing you to seamlessly chain bindings and execution.

```cpp
SqliteDatabaseOwned db(":memory:");
auto stmt = db.prepare("INSERT INTO users VALUES (?, ?);");
stmt.bind(1, 42);          // Resolves to bind_int overload
stmt.bind(2, "Alice");     // Resolves to bind_text overload
stmt.step();               // Executes!
```

## 4. Connection Hooks & Event Handlers

`SqliteDatabaseView` and `SqliteDatabaseOwned` provide zero-overhead, templatized helpers to bind into SQLite's connection-level event pipeline:

### Compile-Time Zero-Overhead Trampolines
```cpp
// Static or free functions without void* boilerplate
void on_update(int op, const char* db, const char* table, sqlite3_int64 rowid) {
    printf("Operation %d on %s.%s, rowid = %lld\n", op, db, table, rowid);
}

int on_commit() {
    return 0; // 0 = allow commit, non-zero = rollback
}

void on_rollback() {
    printf("Transaction rolled back!\n");
}

int on_progress() {
    return 0; // 0 = continue, non-zero = cancel query
}

// Bind directly using compile-time function pointer templates:
SqliteDatabaseOwned db(":memory:");
db.set_update_hook<on_update>();
db.set_commit_hook<on_commit>();
db.set_rollback_hook<on_rollback>();
db.set_progress_handler<on_progress>(100); // Trigger every 100 VM ops
```

### Strongly-Typed `UserData*` Templates
```cpp
struct AppTracker {
    int count = 0;
};

AppTracker tracker;

// Strongly-typed pointer - no manual static_cast<AppTracker*>(void*) needed!
db.set_update_hook<AppTracker>([](AppTracker* t, int op, const char* db, const char* table, sqlite3_int64 rowid) {
    t->count++;
}, &tracker);
```

## 5. Error Inspection, Status & Diagnostics

`SqliteDatabaseView` provides direct methods to inspect error states, transaction status, and perform administrative operations:

```cpp
SqliteDatabaseOwned db(":memory:");

// 1. Error Inspection
int bad_rc = db.exec("SELECT * FROM non_existent_table;");
if (bad_rc != SQLITE_OK) {
    int code = db.errcode();                    // e.g. SQLITE_ERROR (1)
    int ext_code = db.extended_errcode();       // Extended error code
    const char* msg = db.errmsg();              // "no such table: non_existent_table"
    const char* str = SqliteDatabaseView::errstr(bad_rc); // "SQL logic error"
}

// 2. Transaction & Read-Only Status
bool autocommit = db.is_autocommit();           // true if NOT inside an active transaction
bool readonly = db.is_readonly("main");         // checks if attached database is read-only

// 3. Explicit WAL Checkpointing
db.wal_checkpoint("main", SQLITE_CHECKPOINT_PASSIVE);

// 4. Operational Diagnostics
db.busy_timeout(5000);                          // 5s busy timeout
sqlite3_int64 last_id = db.last_insert_rowid(); // Most recent INSERT rowid
sqlite3_int64 rows_mod = db.changes();          // Rows modified by last statement
sqlite3_int64 total_mod = db.total_changes();   // Total modifications since open
db.interrupt();                                 // Thread-safe query cancellation
```



