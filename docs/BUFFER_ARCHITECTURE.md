# Buffer Architecture

The `SqliteBuffer` and `SqliteString` classes serve as foundational infrastructure for developers writing C++ SQLite extensions in constrained, standard-library-free environments.

## The Problem with `std::string`

In a typical C++ application, developers use `std::string` or `std::vector` to hold dynamic data. However:
1. They require `#include <string>` and `#include <vector>`, pulling in massive amounts of template code.
2. They allocate memory using the global C++ `operator new`, bypassing SQLite's custom memory allocators (which track quotas, limit memory usage, and report leaks).
3. If an extension compiled with one standard library (e.g. MSVC) passes a `std::string` across a DLL boundary to an application compiled with another (e.g. MinGW), it can instantly cause memory corruption due to differing ABI layouts.

## The Solution

`SqliteBuffer` completely solves this by directly wrapping SQLite's own memory allocators (`sqlite3_malloc64`, `sqlite3_realloc64`, and `sqlite3_free`).

### Geometric Growth
To avoid calling `realloc` on every single byte appended, `SqliteBuffer` implements standard vector geometric growth. 
When `.append()` exceeds the current `m_capacity`, the capacity is doubled (falling back to the exact required size if doubling isn't enough). This guarantees amortized O(1) append time.

### Zero-Copy Integration (Future Proofing)
Because `SqliteBuffer` uses SQLite's allocator, it unlocks incredible future optimization potential. 
When passing a buffer into a SQLite function that expects a destructor callback (like `sqlite3_result_text64` or `sqlite3_bind_text64`), you can pass `sqlite3_free` as the destructor callback and `buffer.data()` as the pointer, successfully transferring ownership of the memory *directly* to SQLite's core engine without making a single copy!

### Transparent Heterogeneous Lookups & Hashing
`SqliteBuffer` and `SqliteString` implement the full suite of relational operators (`<`, `>`, `<=`, `>=`, `==`, `!=`) against each other, against C-strings, and against polymorphic `SqliteValueView` wrappers.
By explicitly delegating their `hash()` implementation to `SqliteHashUtil::hash()` and their equality checks to `SqliteMemoryUtil::memcmp_equal()`, they perfectly emulate the hashing algorithm of SQLite's `SQLITE_TEXT` and `SQLITE_BLOB` values. 

This guarantees that a `SqliteString` will yield the *exact same hash* as a `sqlite3_value` containing identical text, allowing buffers to serve as high-performance, zero-allocation lookup keys into `std::unordered_map<SqliteValueOwned, T>`!
