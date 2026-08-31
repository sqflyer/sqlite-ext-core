# SQLite Allocator Architecture (`sqlite3_allocator.hpp`)

## 1. The Problem: The `<new>` Header Dependency in Freestanding Environments

In standard C++, dynamically allocating an object requires the `new` keyword, which does two things:
1. Allocates memory (usually via `malloc`).
2. Invokes the C++ constructor on that memory.

If you already have memory (e.g. from `sqlite3_malloc` or an in-situ stack buffer), you must use "placement new" (`new (ptr) Type()`) to invoke the constructor. However, the C++ standard dictates that placement new requires `#include <new>`. In highly constrained extension environments (e.g., `-nostdlib++` and `-fno-exceptions`), including `<new>` triggers linker errors because the compiler expects the standard library to define the placement new operator.

---

## 2. The Solution: Proprietary Tag Trickery

To achieve true zero-dependency C++ construction, `sqlite3_allocator.hpp` defines a proprietary inline `operator new` paired with a dummy struct (`sqlite_new_tag`).

```cpp
struct sqlite_new_tag {};

inline void* operator new(size_t, void* p, sqlite_new_tag) noexcept {
    return p;
}
```

Because the C++ compiler processes the `new` keyword natively as part of the language parser (not the library), this definition tells the compiler exactly how to evaluate `new (ptr, tag) Type()`. It generates raw assembly to invoke the constructor on `ptr` without ever attempting to search for the standard library's `<new>` header.

---

## 3. Core Components

### `sqlite_construct_at` and `sqlite_construct_n`
A wrapper around the proprietary tag trick. It casts the memory and perfectly forwards constructor arguments.
```cpp
template <typename T, typename... Args>
inline T* sqlite_construct_at(T* p, Args&&... args) {
    return new (p, sqlite_new_tag{}) T(sqlite_forward<Args>(args)...);
}

template <typename T, typename... Args>
inline void sqlite_construct_n(T* first, size_t count, Args&&... args) {
    for (size_t i = 0; i < count; ++i) {
        sqlite_construct_at(first + i, args...);
    }
}
```
This entirely hides the `sqlite_new_tag{}` boilerplate from the rest of the codebase, ensuring an API that is identical to C++20's `std::construct_at`.

### `sqlite_new` and `sqlite_delete`
These are the primary entry points for single-object dynamic allocation.
- `sqlite_new` calls `sqlite3_malloc(sizeof(T))`, then safely delegates to `sqlite_construct_at` to initialize the object.
- `sqlite_delete` explicitly invokes the qualified pseudo-destructor (`ptr->T::~T()`) before routing the memory to `sqlite3_free`, ensuring safe destruction while silencing compiler warnings (`-Wdelete-non-abstract-non-virtual-dtor`).

### Consolidated Freestanding Type Traits & Utilities
To support variadic perfect forwarding, move semantics, and template specialization without `<utility>` or `<type_traits>`, the allocator acts as the single canonical repository for all freestanding traits across `sqlite-ext-core`:
- `sqlite_remove_reference<T>`, `sqlite_remove_const<T>`, `sqlite_remove_cv<T>` (Mimics standard type transformations)
- `sqlite_add_rvalue_reference<T>`, `sqlite_declval<T>()` (Unevaluated reference conversion for `decltype`)
- `sqlite_move` / `sqlite_move_ptr` (Mimics `std::move` for rvalue reference casts)
- `sqlite_forward` (Mimics `std::forward` for perfect forwarding)
- `sqlite_is_same<T, U>` (Compile-time type equality trait)
- `sqlite_is_pointer<T>` (Compile-time pointer detection trait)
- `sqlite_enable_if<B, T>` (Freestanding SFINAE conditional type enabler)
- `sqlite_is_trivially_copyable<T>` (Leverages compiler intrinsic `__is_trivially_copyable`)
- `SQLITE_FAST_MEMCPY` (Compiler-optimized memory copy intrinsic using `__builtin_memcpy` on GCC/Clang and `__movsb` on MSVC)

---

## 4. Decoupled Array Architecture

For contiguous arrays, `sqlite3_allocator.hpp` adopts a strict "decoupled memory" model (inspired by C++20's `std::allocator`). Memory allocation is fundamentally separated from object construction to maximize performance and avoid the hidden length overhead associated with the standard `new[]` operator.

1. **`sqlite_new_array`**: Strictly acts as a typed wrapper around `sqlite3_malloc`. It allocates an uninitialized memory buffer and performs integer overflow checks (`size_t` multiplication boundary checks) for safety.
2. **`sqlite_construct_at` / `sqlite_construct_n`**: Invokes constructors only on the active element range.
3. **`sqlite_destroy_at` and `sqlite_destroy_n`**: Safely invoke the C++ pseudo-destructors (`ptr->T::~T()`) on constructed memory without freeing the buffer. Mirrors C++20 `std::destroy_n`.
4. **`sqlite_delete_array`**: Strictly acts as a typed wrapper around `sqlite3_free`.
5. **`sqlite_uninitialized_copy_n` / `sqlite_uninitialized_move_n`**: Range copy and move algorithms for uninitialized storage with trivial type copy elision fast-paths.

By isolating these pure memory primitives into `sqlite3_allocator.hpp`, higher-level abstractions like `sqlite3_row.hpp` and `sqlite3_value_containers.hpp` maintain 100% C ABI compatibility and zero-std compliance.
