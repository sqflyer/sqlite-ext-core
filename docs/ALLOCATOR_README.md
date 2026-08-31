# SQLite Allocator (`sqlite3_allocator.hpp`)

A zero-dependency C++ memory management and perfect-forwarding utility for SQLite extensions.

## Overview

When building C++ SQLite extensions with strict compiler flags like `-nostdlib++` and `-fno-exceptions`, you lose access to the standard C++ `<new>` and `<utility>` headers. This prevents you from natively using the `new` keyword to invoke C++ constructors or using `std::move` and `std::forward` for efficient semantics.

`sqlite3_allocator.hpp` solves this by providing a completely self-contained, SQLite-native memory management API that mirrors standard C++ behavior while strictly routing all memory through `sqlite3_malloc` and bypassing the standard library.

## Features

- **Standard Library Independence**: 100% free of `<new>`, `<utility>`, `<type_traits>`, and `libstdc++`.
- **Memory Profiler Integration**: Routes all allocations through `sqlite3_malloc` and `sqlite3_free`, ensuring your C++ objects respect SQLite's memory hard limits and profilers.
- **In-place Construction**: Provides `sqlite_construct_at` and `sqlite_construct_n` for safe placement-new C++ object initialization without `<new>`.
- **Zero-Dependency Move Semantics**: Includes `sqlite_move` and `sqlite_move_ptr` to completely replace `std::move`.
- **Perfect Forwarding**: Includes `sqlite_forward` to mimic `std::forward`.
- **Freestanding Type Traits**: Single canonical header providing `sqlite_enable_if`, `sqlite_is_same`, `sqlite_is_pointer`, `sqlite_remove_reference`, `sqlite_remove_const`, `sqlite_remove_cv`, `sqlite_add_rvalue_reference`, `sqlite_declval`, and `sqlite_is_trivially_copyable`.
- **SIMD Fast Memory Copy**: Provides `SQLITE_FAST_MEMCPY` optimized across GCC/Clang built-ins (`__builtin_memcpy`) and MSVC intrinsics (`__movsb`).
- **Smart Pointer Ready**: Acts as the foundational memory layer for `SqliteSharedPtr` and `SqliteUniquePtr`.

## Usage

### 1. Allocating Objects

Use `sqlite_new<T>(...)` exactly as you would use the standard `new` keyword. It natively forwards all arguments to your C++ constructor.

```cpp
#include "sqlite3_allocator.hpp"

// Allocates memory via sqlite3_malloc and calls MyClass(10, "test")
MyClass* obj = sqlite_new<MyClass>(10, "test");
```

### 2. Freeing Objects

Use `sqlite_delete(ptr)` exactly as you would use `delete`. It calls your C++ destructor before freeing the memory via `sqlite3_free`.

```cpp
sqlite_delete(obj);
```

### 3. Array Allocation

To allocate an array of objects, use `sqlite_new_array`. Note that for raw performance, it allocates **uninitialized memory** and does not call C++ constructors or destructors automatically.

```cpp
// Allocates raw memory for 100 objects (uninitialized)
MyClass* arr = sqlite_new_array<MyClass>(100);

// In-place construct N elements
sqlite_construct_n(arr, 100, 10, "test");

// Destroy N elements before freeing
sqlite_destroy_n(arr, 100);

// Frees the raw buffer
sqlite_delete_array(arr);
```

### 4. In-Place Construction

If you already have pre-allocated memory (e.g. inside a larger struct or on the stack), use `sqlite_construct_at` to explicitly invoke a C++ constructor on that memory without `#include <new>`.

```cpp
struct Wrapper {
    MyClass obj;
};

Wrapper* w = (Wrapper*)sqlite3_malloc(sizeof(Wrapper));
sqlite_construct_at(&w->obj, 10, "test");
```

### 5. Move Semantics

Use `sqlite_move` (or `sqlite_move_ptr`) in place of `std::move` to transfer ownership without copying.

```cpp
MyClass new_obj = sqlite_move(old_obj);
```
