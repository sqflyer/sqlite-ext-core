# SQLite Smart Pointers (C/C++)

The `sqlite3_smart_ptr` suite provides zero-dependency, thread-safe, reference-counted memory allocation that integrates directly into SQLite's memory manager (`sqlite3_malloc`). It allows you to safely share dynamic payloads across User-Defined Function (UDF) boundaries without memory leaks, race conditions, or violating SQLite's aggressive memory profilers.

---

## Why do I need this?

If you want your SQLite extension to maintain state across function boundaries (like an active connection pool, an LRU cache, or a dynamically sized array from a UDF to an aggregate function), you must allocate memory on the heap.

If you use standard C++ pointers or `std::shared_ptr`, the memory allocation happens outside of SQLite's purview. This has critical consequences:
1. **Memory Profiling**: Allocating via `malloc` or `new` bypasses SQLite's internal limits (`pragma mmap_size`, `SQLITE_LIMIT_MEMORY`).
2. **Double-Frees**: Sharing raw pointers between UDFs often leads to a Use-After-Free or a Double-Free if you aren't perfectly tracking the lifecycle.
3. **C/C++ Interoperability**: `std::shared_ptr` cannot be easily expressed in a C API, making your extension harder to bridge.

These smart pointers solve this by:
- Allocating strictly via `sqlite3_malloc` (ensuring SQLite profiles the memory).
- Guaranteeing fast, 100% thread-safe lifecycle teardown using lock-free atomics.
- Working cleanly in purely freestanding (`no-std`) environments.
- Exposing an identical interface for both pure C and C++.

---

## C++ API (`sqlite3_smart_ptr.hpp`)

The C++ template provides a fully featured smart pointer suite with identical semantics to the standard library, but strictly backed by SQLite.

### 1. `SqliteUniquePtr<T>`
An exclusive-ownership pointer. It has **exactly zero memory overhead** compared to a raw pointer. Use this when a single function or struct owns the payload exclusively.

```cpp
#include "sqlite3_smart_ptr.hpp"

struct MyPayload { int id; };

void unique_example() {
    MyPayload* raw = sqlite_new<MyPayload>();
    
    // Takes ownership. When `up` goes out of scope, it calls ~MyPayload() and sqlite3_free()
    SqliteUniquePtr<MyPayload> up(raw);
    
    up->id = 10;
    
    // Transfer ownership (uses zero-dependency custom move semantics)
    SqliteUniquePtr<MyPayload> up2 = sqlite_move_ptr(up);
    
    // 'up' is now empty, 'up2' exclusively owns the payload.
}
```

### 2. `SqliteSharedPtr<T>`
A thread-safe, reference-counted pointer. Use this when multiple independent components (like multiple UDF calls) need to share read/write access to the same heap object.

```cpp
void shared_example() {
    MyPayload* raw = sqlite_new<MyPayload>();
    
    SqliteSharedPtr<MyPayload> sp1(raw); // Ref count = 1
    {
        // Reference count is atomically bumped up to 2
        SqliteSharedPtr<MyPayload> sp2 = sp1;
    } // sp2 goes out of scope, reference count drops to 1
    
    // Custom Deleters:
    // By default, smart pointers automatically use sqlite_delete. 
    // If you need a custom lifecycle callback:
    MyPayload* raw2 = sqlite_new<MyPayload>();
    SqliteSharedPtr<MyPayload> sp_custom(raw2, [](MyPayload* ptr) {
        // Custom deep clean logic here
        sqlite_delete(ptr);
    });
} // sp1 goes out of scope, reference count drops to 0, memory is safely freed.
```

### 3. `SqliteWeakPtr<T>`
A non-owning observer. Use this to break cyclic references or safely check if a shared payload has already been destroyed by another thread.

```cpp
void weak_example() {
    SqliteSharedPtr<MyPayload> sp1(sqlite_new<MyPayload>());
    
    // Create weak observer
    SqliteWeakPtr<MyPayload> wp(sp1);

    // Safely upgrade to a strong pointer if the object hasn't been deleted yet
    SqliteSharedPtr<MyPayload> locked = wp.lock();
    if (locked) {
        locked->id = 20;
    }
}
```

---

## C API (`sqlite3_smart_ptr.h`)

For pure C extensions, we provide macros that automatically generate strongly-typed smart pointer structs. The generated structs are ABI-compatible across compilers.

### 1. Define the Pointer
```c
#include "sqlite3_smart_ptr.h"

typedef struct { int data; } MyStruct;

// Generate Shared Pointer: MyStruct_SharedPtr
// (This also automatically generates MyStruct_WeakPtr)
// Arg 3 is the destructor used when the ref-count hits 0
SQLITE_SHARED_PTR_DEFINE(MyStruct, MyStruct, sqlite3_free)

// Generate Unique Pointer: MyStruct_UniquePtr
SQLITE_UNIQUE_PTR_DEFINE(MyStruct, MyStruct, sqlite3_free)
```

### 2. Use the Pointer
```c
void test() {
    MyStruct* raw = (MyStruct*)sqlite3_malloc(sizeof(MyStruct));
    
    // Create a Shared Pointer
    MyStruct_SharedPtr sp1 = MyStruct_make_shared(raw);
    
    // Create a Weak Pointer observer
    MyStruct_WeakPtr wp = MyStruct_weak_create(sp1);
    
    // Thread-safe clone (bumps ref count)
    MyStruct_SharedPtr sp2 = MyStruct_clone(sp1);
    
    // Safely transfer ownership WITHOUT bumping ref count (sp1 is zeroed out)
    MyStruct_SharedPtr sp3 = MyStruct_move(&sp1);
    
    // Get raw pointer
    MyStruct* ptr = MyStruct_get(sp3);
    ptr->data = 5;
    
    // Release pointer (drops ref count, zeroes the pointer)
    MyStruct_release(&sp3);
    MyStruct_release(&sp2); // Ref count hits 0 -> Frees the memory
    
    // Safely check if expired, or upgrade to a strong pointer
    if (MyStruct_weak_expired(wp)) {
        // Safe to clean up observer
        MyStruct_weak_release(&wp);
    } else {
        // Atomically upgrades the weak pointer to a full shared pointer
        MyStruct_SharedPtr locked = MyStruct_weak_lock(wp);
        if (locked.cb) {
            // Memory is guaranteed to be alive inside this block
            ptr = MyStruct_get(locked);
            ptr->data = 10;
            
            // Release the upgraded lock
            MyStruct_release(&locked);
        }
        MyStruct_weak_release(&wp);
    }
}
```

---

## Best Practices
- **Never wrap stack variables:** Both C and C++ wrappers strictly assume that the underlying payload was allocated on the heap via `sqlite3_malloc`. If you wrap a stack variable (`int x = 5; SqliteSharedPtr<int> sp(&x);`), your extension will violently crash when the pointer attempts to call `sqlite3_free` on stack memory.
- **Always Check Return Values:** Because the smart pointers allocate a `ControlBlock` dynamically, memory allocation can fail. `SqliteSharedPtr` will automatically invoke the deleter and return an empty wrapper if the internal `sqlite3_malloc` call fails. You should check if the pointer is valid `if (sp1)` before dereferencing it.
