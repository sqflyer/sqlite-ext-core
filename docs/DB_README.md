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
