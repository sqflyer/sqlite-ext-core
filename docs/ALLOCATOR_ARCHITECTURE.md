# SQLite Allocator Architecture

## The Problem: The `<new>` Header Dependency

In standard C++, dynamically allocating an object requires the `new` keyword, which does two things:
1. Allocates memory (usually via `malloc`).
2. Invokes the C++ constructor on that memory.

If you already have memory (e.g. from `sqlite3_malloc`), you must use "placement new" (`new (ptr) Type()`) to invoke the constructor. However, the C++ standard dictates that placement new requires `#include <new>`. In highly constrained extension environments (e.g., `-nostdlib++`), including `<new>` triggers linker errors because the compiler expects the standard library to define the placement new operator.

## The Solution: Proprietary Tag Trickery

To achieve true zero-dependency C++ construction, `sqlite3_allocator.hpp` defines a proprietary inline `operator new` paired with a dummy struct (`sqlite_new_tag`).

```cpp
struct sqlite_new_tag {};

inline void* operator new(size_t, void* p, sqlite_new_tag) noexcept {
    return p;
}
```

Because the C++ compiler processes the `new` keyword natively as part of the language parser (not the library), this definition tells the compiler exactly how to evaluate `new (ptr, tag) Type()`. It generates raw assembly to invoke the constructor on `ptr` without ever attempting to search for the standard library's `<new>` header.

## Components

### `sqlite_construct_at`
A wrapper around the proprietary tag trick. It casts the memory and perfectly forwards constructor arguments.
```cpp
template <typename T, typename... Args>
inline T* sqlite_construct_at(T* p, Args&&... args) {
    return new (p, sqlite_new_tag{}) T(sqlite_forward<Args>(args)...);
}
```
This entirely hides the `sqlite_new_tag{}` boilerplate from the rest of the codebase, ensuring an API that is identical to C++20's `std::construct_at`.

### `sqlite_new` and `sqlite_delete`
These are the primary entry points for dynamic allocation.
- `sqlite_new` calls `sqlite3_malloc(sizeof(T))`, then safely delegates to `sqlite_construct_at` to initialize the object.
- `sqlite_delete` explicitly invokes the pseudo-destructor (`ptr->~T()`) before routing the memory to `sqlite3_free`.

### Zero-Dependency Utilities
To support variadic perfect forwarding in `sqlite_construct_at`, the allocator implements lightweight type traits:
- `sqlite_remove_reference` (Mimics `std::remove_reference`)
- `sqlite_move_ptr` (Mimics `std::move`)
- `sqlite_forward` (Mimics `std::forward`)

## Decoupled Array Architecture

For contiguous arrays, `sqlite3_allocator.hpp` adopts a strict "decoupled memory" model (inspired by C++20's `std::allocator`). Memory allocation is fundamentally separated from object construction to maximize performance and avoid the hidden length overhead associated with the standard `new[]` operator.

1. **`sqlite_new_array`**: Strictly acts as a typed wrapper around `sqlite3_malloc`. It allocates an uninitialized memory buffer and performs integer overflow checks (`size_t` multiplication boundary checks) for safety.
2. **`sqlite_construct_at` (Manual)**: The user manually loops over the array to invoke constructors only when needed.
3. **`sqlite_destroy_at` and `sqlite_destroy_n`**: Safely invoke the C++ pseudo-destructors (`~T()`) on constructed memory without freeing the buffer. Mirrors C++20 `std::destroy_n`.
4. **`sqlite_delete_array`**: Strictly acts as a typed wrapper around `sqlite3_free`.

By isolating these utilities into `sqlite3_allocator.hpp`, complex components like `sqlite3_ext_state.hpp` and `sqlite3_smart_ptr.hpp` can enjoy modern C++ ergonomics while maintaining 100% C ABI compatibility and zero-std compliance.
