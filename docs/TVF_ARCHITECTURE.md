# C++ TVF Framework Architecture (`sqlite3_tvf.hpp`)

This document details the internal design and architectural decisions behind the Table-Valued Function (TVF) wrapper `sqlite3_tvf.hpp`.

---

## Architectural Objectives

1. **Eradicate C-Boilerplate**: SQLite Virtual Tables require a complex orchestration of `sqlite3_module`, `sqlite3_vtab`, and `sqlite3_vtab_cursor` C-structs, alongside over a dozen function callbacks. The goal is to completely hide this behind a single Object-Oriented Iterator.
2. **Automate Query Planner Integration**: The `xBestIndex` method is notoriously difficult to implement correctly. The framework must generically manipulate the planner to ensure arguments are passed correctly without user intervention.
3. **`-nostdlib++` Compatibility**: The framework must guarantee zero heap overhead beyond the required `sqlite3_malloc` allocations, and must avoid linking dependencies on the standard library (such as a global `operator delete`).

---

## Template Metaprogramming Bridge

To avoid inheritance overhead and dynamic dispatch for module registration, the framework uses a templated bridge: `SqliteTvfModule<T>`. 

When `sqlite_define_tvf<MyIterator>(db, "name")` is called, the compiler instantiates a concrete `sqlite3_module` struct populated with static C-callbacks that internally cast the raw SQLite C-pointers directly back into your strongly-typed `MyIterator` instances.

---

## Memory Management (The `operator delete` Avoidance)

Because the project is compiled with `-nostdlib++` (no standard C++ library), throwing objects on the heap is dangerous. If a base class has a virtual destructor, `delete obj;` will silently emit a dependency on the global `operator delete(void*)`, which will cause a linker error.

To solve this, `SqliteTvfIterator` explicitly avoids virtual destructors:
```cpp
class SqliteTvfIterator {
protected:
    ~SqliteTvfIterator() = default; // Non-virtual and protected!
};
```

When SQLite asks the module to destroy a cursor (`xClose`), the framework mathematically sidesteps the need for a virtual destructor by downcasting the base pointer back to the exact derived type `T*` *before* destruction:
```cpp
static int xClose(sqlite3_vtab_cursor* pCursor) {
    Cursor* pCur = reinterpret_cast<Cursor*>(pCursor);
    
    // 1. Explicitly call the derived class destructor!
    pCur->iter->~T(); 
    
    // 2. Free the memory using SQLite's allocator
    sqlite3_free(pCur->iter);
    sqlite3_free(pCur);
    return SQLITE_OK;
}
```
This guarantees 100% safe destruction of derived members without requiring a vtable or standard library linkage.

---

## Generic `xBestIndex` Auto-Tuning

SQLite TVFs accept parameters by defining them as `HIDDEN` columns in the schema. When a user runs `SELECT * FROM my_tvf(1, 2)`, SQLite treats this as `SELECT * FROM my_tvf WHERE start=1 AND stop=2`.

The `SqliteTvfModule<T>::xBestIndex` method automatically maps these constraints:
1. It loops through all `aConstraint` filters.
2. If it finds a constraint on a hidden column (where `iColumn > 0` for our TVF design), it checks if it is `usable`.
3. If SQLite proposes a query plan where a required parameter is *unusable* (e.g. because it relies on a table that hasn't been `JOIN`ed yet), the framework instantly returns `SQLITE_CONSTRAINT` to forcefully reject the plan.
4. Otherwise, it maps the parameter into the `argvIndex` so that SQLite will pass it to `xFilter` (which routes it to `init()`).

### The Cost Heuristic
Because the framework does not know the algorithmic complexity of the user's C++ class, it injects a highly aggressive heuristic to manipulate the SQLite query optimizer:
```cpp
pIdxInfo->estimatedCost = T::estimated_cost(usable_constraints);
```
By default, the `estimated_cost` formula is `100000.0 / (usable_constraints + 1)`. This guarantees that SQLite will always prioritize query plans (like Correlated Subqueries) that provide the maximum number of arguments to the TVF. Users can override this by defining `static double estimated_cost(int)` in their derived class (resolved purely via C++ static name hiding).
