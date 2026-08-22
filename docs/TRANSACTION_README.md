# C++ Exception-Safe Transactions (`sqlite3_transaction.hpp`)

A zero-dependency C++ RAII wrapper for SQLite Transactions and Savepoints. It guarantees that database transactions are safely rolled back if they go out of scope before being explicitly committed, completely preventing database locking bugs caused by exceptions or early returns.

## Features
- **Strict RAII Lifecycle**: Destructors automatically execute `ROLLBACK` if the transaction is still active.
- **Nested Transactions**: Full support for nested SQLite transactions using the `SqliteSavepoint` wrapper.
- **Zero Allocations**: Built perfectly for `-nostdlib++`. All string interpolations use `sqlite3_mprintf` to safely escape identifiers without pulling in `<string>`.
- **Multiple Behaviors**: Supports `DEFERRED` (default), `IMMEDIATE`, and `EXCLUSIVE` transaction locks.

## Setup
Include the header in your C++ SQLite extension project:
```cpp
#include "include/sqlite3_transaction.hpp"
```

---

## 1. Basic Transactions (`SqliteTransaction`)

The `SqliteTransaction` wrapper executes a `BEGIN` statement on construction.

### The Safety Guarantee
If an error occurs, or you hit an early `return`, the destructor will fire and automatically `ROLLBACK` your changes. 
```cpp
void insert_safe(sqlite3* db) {
    SqliteTransaction txn(db); // Executes "BEGIN DEFERRED;"
    
    int rc = sqlite3_exec(db, "INSERT INTO users VALUES (1);", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        return; // Destructor runs and safely executes "ROLLBACK;"!
    }
    
    // Explicitly commit to save the data!
    txn.commit(); // Executes "COMMIT;"
}
```

### Transaction Behaviors
You can alter the locking behavior using the `SqliteTransactionBehavior` enum:
```cpp
// DEFERRED (Default): Doesn't acquire a lock until the first read/write
SqliteTransaction t1(db);

// IMMEDIATE: Acquires a write lock immediately, preventing other writers
SqliteTransaction t2(db, SqliteTransactionBehavior::IMMEDIATE);

// EXCLUSIVE: Acquires an exclusive lock immediately, preventing readers AND writers
SqliteTransaction t3(db, SqliteTransactionBehavior::EXCLUSIVE);
```

---

## 2. Nested Transactions (`SqliteSavepoint`)

Because SQLite does not allow nested `BEGIN` statements, you must use `SAVEPOINT`s if you want to isolate inner blocks of logic while already inside an active transaction. 

`SqliteSavepoint` behaves identically to `SqliteTransaction`, but it issues `SAVEPOINT`, `RELEASE`, and `ROLLBACK TO` commands under the hood.

```cpp
void complex_operation(sqlite3* db) {
    SqliteTransaction txn(db); // Outer transaction
    
    // ... do some work ...
    
    {
        // Inner transaction using a savepoint
        SqliteSavepoint sp(db, "my_inner_work");
        
        // ... do inner work ...
        
        // If we hit an error here, the destructor will issue:
        // ROLLBACK TO "my_inner_work";
        if (error) return; 
        
        // Explicitly commit the inner work:
        sp.release(); // Executes RELEASE "my_inner_work";
    }
    
    txn.commit(); // Commits everything to the database
}
```

### Safety and Identifier Escaping
You do not need to worry about SQL injection or syntax errors if your savepoint name contains weird characters or quotes. The `SqliteSavepoint` internally uses SQLite's `%w` formatting string inside `sqlite3_mprintf` to safely escape all double-quotes within the identifier name.
