# Database Architecture

The Database ecosystem inside `sqlite-ext-core` is carefully designed to bridge the gap between low-level SQLite C-APIs and modern C++ development semantics without sacrificing any performance.

## The Owned vs View Pattern

SQLite connections often suffer from ownership ambiguity in standard codebases. If a function is handed a `sqlite3*` pointer, it is rarely clear whether the function is responsible for calling `sqlite3_close()`.

To solve this, we implemented the **Owned vs View pattern**, heavily inspired by `std::unique_ptr` and `std::string_view`:

1. **`SqliteDatabaseView` (The Base Class)**
   - Holds the 8-byte `sqlite3*` pointer.
   - Provides all the convenience methods (`.prepare()`, `.exec()`, `operator sqlite3*()`).
   - Does **not** have a destructor. It never closes the connection.

2. **`SqliteDatabaseOwned` (The Derived Class)**
   - Inherits publicly from `SqliteDatabaseView`.
   - The constructor wraps `sqlite3_open_v2`.
   - The destructor explicitly calls `sqlite3_close_v2`.
   - Explicitly deletes copy constructors to prevent double-freeing the handle.
   - Provides move constructors (`std::move`) to safely transfer ownership across scopes.

### The Power of C++ Polymorphism

Because `SqliteDatabaseOwned` inherits from `SqliteDatabaseView`, C++ object slicing guarantees that you can pass an Owned database into any function expecting a View.

```cpp
// This function doesn't take ownership!
void process_data(SqliteDatabaseView db) {
    db.exec("...");
}

int main() {
    SqliteDatabaseOwned my_db("file.db");
    
    // Pass the Owned DB into a View argument safely!
    process_data(my_db); 
}
```

This guarantees at compile-time that `process_data` cannot accidentally close your database handle, while still granting it full access to all C++ helper methods. Furthermore, this object slicing costs exactly zero runtime overhead at `-O2`, translating directly into a raw C pointer copy.

Both `SqliteDatabaseView` and `SqliteTransaction` expose `.prepare(sql)`. This tightly integrates the ecosystem, ensuring that C++ developers never have to touch a raw `sqlite3*` pointer if they don't want to.

## Connection Hooks & Compile-Time Trampoline Architecture

Managing database-level event hooks in C traditionally requires manual `void*` pointer unpacking and static C-style callback signatures.

`SqliteDatabaseView` implements a dual-layer abstraction:

### 1. Compile-Time Function Pointer Trampolines (`<Func>`)
Leverages static inline structs inside template methods to generate zero-overhead C-dispatch wrappers at compile-time:

```cpp
template <void (*Func)(int, const char*, const char*, sqlite3_int64)>
inline void* set_update_hook() const {
    struct Trampoline {
        static void callback(void*, int op, const char* db, const char* tbl, sqlite3_int64 rowid) {
            Func(op, db, tbl, rowid);
        }
    };
    return sqlite3_update_hook(m_db, Trampoline::callback, nullptr);
}
```
- **Zero Runtime Overhead**: The compiler inlines `Trampoline::callback` directly into the SQLite dispatch table.
- **No `void*` Boilerplate**: Developers write clean, idiomatic C++ functions without receiving or casting unused context pointers.

### 2. Strongly-Typed UserData Templates (`<UserData>`)
Allows passing strongly typed context objects (`UserData*`) directly to callbacks without unsafe `reinterpret_cast` or `static_cast<T*>(void*)` in client code:

```cpp
template <typename UserData>
inline void* set_commit_hook(int (*cb)(UserData* user_data), UserData* user_data = nullptr) const {
    typedef int (*RawCommitCb)(void*);
    return sqlite3_commit_hook(m_db, reinterpret_cast<RawCommitCb>(cb), static_cast<void*>(user_data));
}
```

### 3. Supported Event Pipelines & Connection Diagnostics
- **Update Hook (`set_update_hook`)**: Change-Data-Capture (CDC) for `SQLITE_INSERT`, `SQLITE_UPDATE`, `SQLITE_DELETE`.
- **Commit Hook (`set_commit_hook`)**: Transaction interceptor returning 0 to commit or non-zero to abort/rollback.
- **Rollback Hook (`set_rollback_hook`)**: Notification pipeline for rollback events.
- **WAL Hook (`set_wal_hook`)**: Write-Ahead-Log frame checkpointing notifications.
- **Progress Handler (`set_progress_handler`)**: Periodic VM instruction tick counter for query timeouts and thread cancellation (`SQLITE_INTERRUPT`).
- **WAL Checkpointing (`wal_checkpoint`)**: Administrative passive/full/restart/truncate checkpoint execution.
- **Transaction & Schema Introspection**: `is_autocommit()` to check active transaction state, `is_readonly(zDb)` to check read-only status.
- **Error Diagnostics**: `errcode()`, `extended_errcode()`, `errmsg()`, and static `errstr(rc)`.
- **Row Metrics**: `last_insert_rowid()`, `changes()`, `total_changes()`, `busy_timeout(ms)`, `interrupt()`.


