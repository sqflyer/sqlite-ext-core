# Database Architecture

The Database ecosystem inside `c-sqlite-ext-core` is carefully designed to bridge the gap between low-level SQLite C-APIs and modern C++ development semantics without sacrificing any performance.

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

## Integration with Statements and Transactions

Rather than forcing developers to manually inject raw database pointers into Statements and Transactions, the Database wrappers provide fluent builder methods. 

Both `SqliteDatabaseView` and `SqliteTransaction` expose `.prepare(sql)`. This tightly integrates the ecosystem, ensuring that C++ developers never have to touch a raw `sqlite3*` pointer if they don't want to.
