# Smart Pointer Architecture

The `sqlite3_smart_ptr` suite is engineered to provide identical semantics to the C++ Standard Library (`std::shared_ptr`, `std::unique_ptr`), but strictly rewritten to comply with the performance and memory constraints of SQLite extensions.

## Core Architectural Differences from STL

1. **Memory Allocation Profiling**: The STL allocates control blocks using the global `new` operator. If an extension allocates vast amounts of memory using `new`, SQLite's internal memory limiters (`pragma mmap_size`, `SQLITE_LIMIT_MEMORY`) are entirely bypassed. `sqlite3_smart_ptr` strictly uses `sqlite3_malloc` to ensure all dynamic payloads are properly tracked by the engine.
2. **Lock Selection**: STL shared pointers generally use atomic operations for reference counting. While atomics are fast, they are not always available across diverse SQLite compilation targets (e.g., specific WASM environments or older C standard compilers). To guarantee 100% portability while minimizing overhead, we built `SqliteTinyLock`—a custom 4-byte hybrid spinlock. It spins natively on hardware (using CPU pause instructions) and sleeps intelligently on WebAssembly. Crucially, because it is only 4 bytes, it is embedded by value directly into the Control Block, completely eliminating the secondary heap allocation that a native `sqlite3_mutex` would require.

## Zero-Dependency (`no-std`) Engineering

One of the largest challenges in this library was providing modern C++ semantics without linking to the C++ Standard Library (`libstdc++` or `<utility>`).

### 1. Pseudo-Destructors instead of `<new>`
Since we compile with `-fno-exceptions` and `-nostdlib++`, we cannot rely on the STL `delete` keyword. We destroy objects by explicitly invoking their destructor and then freeing the memory.
```cpp
ptr->~T();         // Trigger the C++ destructor natively
sqlite3_free(ptr); // Free the underlying memory allocation
```

### 2. `sqlite_move_ptr` instead of `std::move`
`std::move` is typically provided by `<utility>` and relies on `<type_traits>` (specifically `std::remove_reference`). To bypass this, we implemented a custom, purely language-level reference stripping template:
```cpp
template<typename T> struct sqlite_remove_reference { typedef T type; };
template<typename T> struct sqlite_remove_reference<T&> { typedef T type; };
template<typename T> struct sqlite_remove_reference<T&&> { typedef T type; };

// Safely cast to an rvalue reference (T&&) without relying on the STL
template<typename T>
inline typename sqlite_remove_reference<T>::type&& sqlite_move_ptr(T&& arg) noexcept {
    return static_cast<typename sqlite_remove_reference<T>::type&&>(arg);
}
```

---

## The Control Block Lifecycle

When a `SqliteSharedPtr` is instantiated, it allocates a `SqlitePtrControlBlock`.

```cpp
template<typename T>
struct SqlitePtrControlBlock {
    T* ptr;
    int strong_count;
    int weak_count;
    SqliteTinyLock mutex;
    void (*deleter)(T*);
};
```

This block is the heart of the thread-safe garbage collector.

### Separation of Lifecycles
Unlike a `UniquePtr` which has a simple 1:1 lifecycle with the object, the Control Block manages a 1:N lifecycle by tracking two separate counters:

1. **`strong_count`**: The number of active `SqliteSharedPtr`s. When this count reaches `0`, the managed object (`ptr`) is destroyed via the `deleter`. The memory of the object is freed, but the *Control Block itself stays alive*.
2. **`weak_count`**: The number of active `SqliteWeakPtr` observers. `SqliteWeakPtr`s do not prevent the object from being destroyed. However, they need to know if the object is still alive. By keeping the Control Block alive after the object dies, the Weak Pointers can safely check if `strong_count == 0` without hitting a Use-After-Free segfault.

The Control Block itself is only destroyed and deallocated (`sqlite3_free(m_cb)`) when **both** `strong_count` and `weak_count` hit 0.

### State Transition Diagram

```text
             SqliteSharedPtr(ptr)
     [*] ---------------------------> +--------+
                                      | Active |<----+
                                      +--------+     | clone() [strong++]
                                          |          |
                                          +----------+
                                          |
                                          | release()
                    +---------------------+---------------------+
                    | [strong == 0, weak > 0]                   | [strong == 0, weak == 0]
                    v                                           v
             +-------------+    weak_release() [weak == 0]   +---------+
             | ObjectFreed | ------------------------------> |   [*]   |
             +-------------+                                 +---------+

Note: In ObjectFreed state, the T* ptr payload is freed. 
The ControlBlock remains in memory until all WeakPtrs expire.
```

---

## Memory Overhead Analysis

### Unique Pointers
`SqliteUniquePtr` is fundamentally different from `SqliteSharedPtr`. Since it guarantees exclusive ownership, it **does not allocate a control block** and **does not use mutexes**. 

A `SqliteUniquePtr` compiles down to exactly the size of a raw C-pointer plus the function pointer to its custom deleter (16 bytes on a 64-bit architecture). It has absolutely zero dynamic allocation overhead compared to manual `sqlite3_free` management, while providing full exception safety and RAII lifecycle automation.

### Shared Pointers
`SqliteSharedPtr` requires a separate heap allocation for the `ControlBlock` to track references across threads.
- `T* ptr`: 8 bytes
- `int strong_count`: 4 bytes
- `int weak_count`: 4 bytes
- `SqliteTinyLock mutex`: 4 bytes
- `void (*deleter)`: 8 bytes
- **Total Overhead**: 28 Bytes per shared instance (often padded to 32 bytes).

By embedding `SqliteTinyLock` by value rather than allocating a `sqlite3_mutex`, we reduced the number of heap allocations per `SharedPtr` from 2 down to exactly 1. This drastically reduces memory fragmentation and speeds up the `make_shared` instantiation. This overhead is only paid **once** per payload, regardless of how many `SharedPtr` or `WeakPtr` instances refer to it.

---

## C Macro Architecture (`sqlite3_smart_ptr.h`)

To provide pure C developers with strongly-typed pointers without relying on dangerous `void*` casting, the `SQLITE_SHARED_PTR_DEFINE` macro dynamically generates fully formed structs and functions uniquely named for your type.

### 1. Token Pasting and Namespacing
The macro uses the C preprocessor's token pasting operator (`##`) to automatically namespace all generated structs and functions. If you pass `MyStruct` as the Prefix, it generates `MyStruct_SharedPtr`, `MyStruct_WeakPtr`, and functions like `MyStruct_weak_lock`. This prevents global symbol collisions, allowing you to define smart pointers for dozens of different types in the same codebase without them clashing.

### 2. Static Destructor Injection (Zero Memory Overhead)
In the C++ `SqliteSharedPtr`, supporting custom deleters requires storing a function pointer (`void (*deleter)(T*)`) dynamically at runtime inside the Control Block. This adds 8 bytes of memory overhead per allocation. 
In the C Macro, the `Destructor` argument is resolved at *compile-time*. The macro literally injects the destructor (e.g., `sqlite3_free(cb->ptr)`) directly into the generated `_release` function's source code. This means the C macro Control Block does NOT need to store a function pointer, saving memory and executing faster!

### 3. Absolute Type Safety
Since C lacks C++ templates, a polymorphic `void*` based memory manager would completely sacrifice compile-time type safety. By passing the `Prefix` and `Type` directly into the macro, the generated `ControlBlock` directly stores a strongly typed `Type* ptr`. When you call `Prefix_get(sp)`, it natively returns a strongly-typed pointer, entirely avoiding runtime casts or pointer mismatch errors.

### 4. Linker-Safe Aggressive Inlining
Every single generated function is declared as `static inline`. 
- **Performance**: This guarantees that modern C compilers (GCC, Clang) will completely inline the reference counting logic directly into the caller's assembly, achieving absolute zero-overhead execution paths compared to hand-written manual reference counting.
- **Linker Safety**: Because they are `static`, multiple different `.c` files (translation units) can invoke `SQLITE_SHARED_PTR_DEFINE` for the exact same struct without causing "Multiple Definition" linker errors. Each translation unit simply gets its own safely isolated, inlined copy of the fast-path logic.
