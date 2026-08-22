# TinyLock (`sqlite3_tiny_lock`)

TinyLock is a blisteringly fast, 1-byte, zero-dependency hybrid spinlock for C and C++. 

It was built specifically for `c-sqlite-ext-core` to provide a portable thread-safety primitive without relying on `<atomic>`, `libstdc++`, or bulky OS-level mutexes like `pthread_mutex_t` or `CRITICAL_SECTION`. 

## Features
- **Zero Dependencies**: Requires absolutely no OS headers or standard library files.
- **Microscopic Size**: Exactly 1 byte in size on native hardware (4 bytes on WASM).
- **Cross-Platform**: Natively detects and optimizes for MSVC (Windows), GCC, and Clang (macOS, Linux, iOS, Android).
- **Hybrid Behavior**: Runs as a highly optimized, cache-friendly TTAS (Test and Test-And-Set) spinlock on native hardware, and automatically transforms into a true 0% CPU sleeping mutex on WebAssembly.
- **Dual API**: Provides a pure C struct and a modern C++ RAII class.

*Note: TinyLock is built on top of [sqlite3_atomic.h](ATOMIC_README.md), a fully featured suite of zero-dependency cross-platform memory primitives.*

## Usage in C (`sqlite3_tiny_lock.h`)

The C API is lightweight and relies on standard function calls passing a pointer to the lock struct.

```c
#include "sqlite3_tiny_lock.h"

// 1. Declare the struct (1 byte on native, 4 bytes on WASM)
sqlite3_tiny_lock my_lock;

void do_work() {
    // 2. Initialize before use
    sqlite3_tiny_lock_init(&my_lock);

    // 3. Acquire the lock (blocks until available)
    sqlite3_tiny_lock_lock(&my_lock);
    
    // ... Thread-safe operations ...

    // 4. Release the lock
    sqlite3_tiny_lock_unlock(&my_lock);
}
```

## Usage in C++ (`sqlite3_tiny_lock.hpp`)

The C++ API provides a zero-overhead class wrapper and a standard RAII `LockGuard` for exception-safe unlocking.

```cpp
#include "sqlite3_tiny_lock.hpp"

// 1. Declare the C++ class (still exactly 1 byte on native!)
SqliteTinyLock my_lock;

void do_work() {
    // 2. Automatically lock upon entering scope
    SqliteTinyLockGuard guard(my_lock);
    
    // ... Thread-safe operations ...

    // 3. Automatically unlocked when 'guard' goes out of scope
} // <-- Unlocks here, even if an early return happens!
```

## Integration
TinyLock is currently used as the foundational locking primitive for the `SqliteSharedPtr` memory management system, embedding directly into the control block to save a heap allocation.
