# Virtual Table Architecture

The `sqlite3_module` framework in SQLite is incredibly powerful but incredibly hostile to type-safe languages like C++.

## The Problem
A SQLite virtual table module is an array of raw function pointers (`xCreate`, `xConnect`, `xBestIndex`, `xOpen`, etc.). Every one of these functions is passed a type-erased `sqlite3_vtab*` or `sqlite3_vtab_cursor*` C struct. 

In pure C, developers subclass these by declaring a struct whose very first element is `sqlite3_vtab`, which guarantees standard-layout pointer arithmetic allows casting back and forth.

```c
// The C way
struct MyVTab {
    sqlite3_vtab base;
    int custom_state;
};
```

In C++, doing this with classes containing `virtual` methods breaks standard layout guarantees due to the hidden `vptr` (vtable pointer) injected by the compiler. Casting `sqlite3_vtab*` to a polymorphic C++ object yields **Undefined Behavior**.

## The Architecture Solution

`sqlite3_vtab.hpp` circumvents this issue entirely by wrapping the C++ objects in a standard-layout C-struct dynamically allocated by the module router:

```cpp
template<typename VTableType>
class SqliteVTabModule {
private:
    struct TableWrapper {
        sqlite3_vtab base;
        VTableType* instance; // Safe C++ pointer!
    };
    
    struct CursorWrapper {
        sqlite3_vtab_cursor base;
        SqliteVTabCursor* instance; // Safe C++ pointer!
    };
```

When SQLite invokes `xOpen(sqlite3_vtab_cursor* pCursor)`, the C++ router safely casts it back to `CursorWrapper*`, pulls out the typed `instance`, and dynamically dispatches to `instance->open()`.

This guarantees 100% strict compliance with the C++ standard while completely shielding the developer from SQLite's memory allocator requirements.

## Abstraction Mapping

The routing framework takes raw, error-prone C-structs and converts them into safe C++ types before invoking the polymorphic methods:

- **`xBestIndex`**: SQLite passes a raw `sqlite3_index_info*`. The router wraps this in a `SqliteIndexInfo` class that exposes safe `info.constraint(i)` and `info.usage(i)` array bindings, preventing pointer math errors when configuring indices for operators like `MATCH`.
- **`xFindFunction`**: SQLite passes function pointers and out-arguments. The router expects you to return a clean `SqliteFunctionDef(my_func, args)` struct which it safely decomposes into the raw out-parameters for SQLite.
- **`xUpdate`**: SQLite passes `(int argc, sqlite3_value** argv)`. The router maps this to a bounds-safe `SqliteUdfArgs` wrapper, preventing segfaults from out-of-bounds column access during `INSERT` or `UPDATE` operations.
