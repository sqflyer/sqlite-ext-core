# C++ Table-Valued Function (TVF) Framework (`sqlite3_tvf.hpp`)

A zero-boilerplate, zero-dependency C++ framework for building eponymous SQLite Table-Valued Functions (Virtual Tables). It completely abstracts away the complex C-API (`sqlite3_module`, `sqlite3_vtab`, `xBestIndex`) allowing you to create powerful TVFs by simply implementing a single C++ iterator class.

## Features
- **Zero C-API Boilerplate**: No need to manually define `sqlite3_module` structs or write complex `xConnect`/`xBestIndex`/`xFilter` callbacks.
- **Auto-Routing Arguments**: Hidden columns in your schema are automatically routed directly into your `init(args)` method as `SqliteUdfArgs`.
- **Query Planner Auto-Tuning**: Automatically handles SQLite's `xBestIndex` constraint logic to guarantee the optimizer passes the maximum number of arguments to your function, enabling perfect Correlated Subqueries (e.g. `JOIN tvf(t.id)`).
- **Zero VTable Overhead**: The framework intentionally avoids virtual destructors, ensuring complete compatibility with `-nostdlib++` environments without requiring a global `operator delete`.

## Setup
Include the header in your C++ SQLite extension project:
```cpp
#include "include/sqlite3_tvf.hpp"
```

---

## Example: Building `generate_series`

To build a TVF, you simply inherit from `SqliteTvfIterator` and implement the pure virtual methods.

### 1. Define the Iterator
```cpp
struct SeriesIterator : public SqliteTvfIterator {
    sqlite3_int64 m_current;
    sqlite3_int64 m_stop;
    sqlite3_int64 m_step;

    // 1. Define the schema (Hidden columns become your function arguments!)
    static constexpr const char* schema() {
        return "CREATE TABLE x(value, start HIDDEN, stop HIDDEN, step HIDDEN)";
    }

    // 2. Initialize your iterator with the arguments provided by SQLite
    void init(SqliteUdfArgs args) override {
        m_current = args.size() > 0 && args[0].type() != SQLITE_NULL ? args[0].as_int64() : 0;
        m_stop    = args.size() > 1 && args[1].type() != SQLITE_NULL ? args[1].as_int64() : 0;
        m_step    = args.size() > 2 && args[2].type() != SQLITE_NULL ? args[2].as_int64() : 1;
        if (m_step == 0) m_step = 1; 
    }

    // 3. Advance to the next row
    void next() override {
        m_current += m_step;
    }

    // 4. Check if finished
    bool eof() const override {
        return (m_step > 0) ? (m_current > m_stop) : (m_current < m_stop);
    }

    // 5. Return the current column value
    void column(sqlite3_context* ctx, int iCol) override {
        if (iCol == 0) { // The 'value' column
            sqlite3_result_int64(ctx, m_current);
        }
    }
};
```

### 2. Register the TVF
Registration is a one-liner! The framework automatically generates the entire C-module based on your class type `T`.
```cpp
void init_my_extension(sqlite3* db) {
    sqlite_define_tvf<SeriesIterator>(db, "my_series");
}
```

### 3. Execute
```sql
SELECT value FROM my_series(1, 10, 2);
-- Output: 1, 3, 5, 7, 9
```

---

## Advanced Feature: Custom Planner Cost
The SQLite query planner asks your TVF how "expensive" it is to run so it can choose the most efficient `JOIN` strategy. 

By default, the framework tells SQLite that the cost is **inversely proportional to the number of arguments provided**. This golden heuristic guarantees that SQLite will prioritize passing you the maximum number of arguments possible.

However, if you want to override this, you can simply define a `static double estimated_cost(int)` method in your class. C++ static name hiding will automatically route the query planner to your custom cost function with absolutely zero virtual overhead!

```cpp
struct CustomIterator : public SqliteTvfIterator {
    // ... schema, init, etc.

    // Override the default query planner heuristic!
    static double estimated_cost(int usable_args) {
        return (usable_args == 0) ? 9999999.0 : 10.0;
    }
};
```

---

For internal architectural details, please see [TVF_ARCHITECTURE.md](TVF_ARCHITECTURE.md).
