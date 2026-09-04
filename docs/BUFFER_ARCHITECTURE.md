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

## The `SqliteString` Architecture
`SqliteString` implements this trivially by protectedly inheriting from `SqliteBuffer` and manually ensuring that the very last byte of `m_data` is always forcibly set to `'\0'`. It hides all raw `append` methods, replacing them with typed methods that guarantee null-termination after every mutation.

## The `SqliteBufferSlice` Architecture
To prevent devastating memory allocations when parsing or tokenizing large buffers, `SqliteBufferSlice` provides a non-owning window over memory. It holds nothing but a `const void*` pointer and a `size`. 

Because it is equipped with the exact same SIMD-accelerated FNV-1a hash and `memcmp` relational operators as `SqliteBuffer`, you can extract a slice of a 100MB buffer and use it directly as a key in a `std::unordered_map` without ever deep-copying a single byte.

### Zero-Copy Integration (Future Proofing)
Because `SqliteBuffer` uses SQLite's allocator, it unlocks incredible future optimization potential. 
When passing a buffer into a SQLite function that expects a destructor callback (like `sqlite3_result_text64` or `sqlite3_bind_text64`), you can pass `sqlite3_free` as the destructor callback and `buffer.data()` as the pointer, successfully transferring ownership of the memory *directly* to SQLite's core engine without making a single copy!

### Transparent Heterogeneous Lookups & Hashing
`SqliteBuffer` and `SqliteString` implement the full suite of relational operators (`<`, `>`, `<=`, `>=`, `==`, `!=`) against each other, against C-strings, and against polymorphic `SqliteValueView` wrappers.
By explicitly delegating their `hash()` implementation to `SqliteHashUtil::hash()` and their equality checks to `SqliteMemoryUtil::memcmp_equal()`, they perfectly emulate the hashing algorithm of SQLite's `SQLITE_TEXT` and `SQLITE_BLOB` values. 

This guarantees that a `SqliteString` will yield the *exact same hash* as a `sqlite3_value` containing identical text, allowing buffers to serve as high-performance, zero-allocation lookup keys into `std::unordered_map<SqliteValueOwned, T>`!

## OOM Resilience & Null-State Safety (`-fno-exceptions`)

In freestanding C++ without exceptions, runtime allocators (`sqlite3_realloc64`) can return `nullptr` under heavy memory pressure.

1. **Non-Throwing Geometric Growth**: If `ensure_capacity()` fails during an append, the method cleanly returns `false` without corrupting existing buffered data.
2. **Deterministic Null States**: Empty or unallocated buffers maintain `m_data == nullptr` and `m_size == 0`. Hashing an empty buffer safely hashes a null pointer (`SqliteHashUtil::hash(nullptr, 0)`), and comparisons against null or empty slices succeed deterministically.
3. **Explicit Verification (`is_valid()`)**: Callers can verify allocation status via `is_valid()` or explicit boolean conversions (`if (buffer)` / `if (str)`).

## Fallible Operations & Rust-Style Error Architecture

For strict zero-exception environments:
- **`SqliteString::try_create(str)`**: Returns [`SqliteResult<SqliteString>`](file:///c:/msys64/home/dilipvamsi/works/repos/sqlite-ext-core/include/sqlite3_allocator.hpp#L882-L994).
- **`SqliteBuffer::try_reserve(cap)`** and **`SqliteBuffer::try_append(...)`**: Return [`SqliteStatus`](file:///c:/msys64/home/dilipvamsi/works/repos/sqlite-ext-core/include/sqlite3_allocator.hpp#L775-L863).
- Conforms to standard Rust-style conventions: `is_err()`, `err_code()`, `err_message()`, and `SqliteResult::err()`.
- Supports monadic chaining (`.map()`, `.and_then()`, `.or_else()`) and early-return macro propagation (`SQLITE_TRY_ASSIGN`, `SQLITE_TRY`).
