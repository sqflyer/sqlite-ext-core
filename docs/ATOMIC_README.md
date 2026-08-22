# Atomic Primitives (`sqlite3_atomic.h`)

The `sqlite3_atomic.h` header provides a zero-dependency, cross-platform suite of atomic memory operations. 

It is designed to give C and C++ SQLite extensions full thread-safety and atomic memory manipulation without relying on `<stdatomic.h>` (which has poor support on older C compilers) or `<atomic>` (which is unavailable when compiling C++ with `-nostdlib++` or `-fno-exceptions`).

## Features
- **Zero Dependencies**: Requires absolutely no OS headers or standard library files.
- **Cross-Platform**: Natively detects and optimizes for MSVC (Windows), GCC, and Clang (macOS, Linux, iOS, Android, WebAssembly).
- **Strongly Typed**: Provides explicitly sized operations for 8-bit, 16-bit, 32-bit, 64-bit, and Pointer types to prevent accidental memory truncation on strict compilers.

## C API Reference (`sqlite3_atomic.h`)

The C library provides the following atomic operations, explicitly sized for your specific data types (8, 16, 32, 64-bit, and PTR).

### 1. Atomic Store and Load
*   `sqlite_atomic_store_8 / 16 / 32 / 64 / ptr(ptr, val)`: Atomically overwrites a memory address.
*   `sqlite_atomic_load_8 / 16 / 32 / 64 / ptr(ptr)`: Atomically reads a memory address with full memory barriers.

```c
#include "sqlite3_atomic.h"

int32_t my_counter = 0;

void reset_counter() {
    sqlite_atomic_store_32(&my_counter, 0);
}
```

### 2. Compare-And-Swap (CAS)
Atomically compares the current value in memory with an `expected` value. If they match, it replaces it with the `desired` value and returns `1` (Success). If they do not match, it updates the `expected` variable with the *actual* value currently in memory and returns `0` (Failure).

**Weak CAS** (Best for loops):
*   `sqlite_atomic_cas_weak_8 / 16 / 32 / 64 / ptr`

**Strong CAS** (Best for single-try attempts):
*   `sqlite_atomic_cas_strong_8 / 16 / 32 / 64 / ptr`

```c
int32_t state = 0; // 0 = Unlocked, 1 = Locked

void lock() {
    int expected = 0;
    // Keep trying to swap 0 for 1 until successful
    while (!sqlite_atomic_cas_weak_32(&state, &expected, 1)) {
        expected = 0; // Reset for the next attempt
    }
}
```

### 3. Exchange (Unconditional Swap)
Unconditionally stores a new value and returns the *old* value.
*   `sqlite_atomic_exchange_8 / 16 / 32 / 64 / ptr(ptr, val)`

### 4. Arithmetic (Increment / Decrement / Fetch)
*   `sqlite_atomic_increment_8 / 16 / 32 / 64(ptr)`: Adds 1 and returns the *new* value.
*   `sqlite_atomic_decrement_8 / 16 / 32 / 64(ptr)`: Subtracts 1 and returns the *new* value.
*   `sqlite_atomic_fetch_add_8 / 16 / 32 / 64(ptr, val)`: Adds `val` and returns the *old* value.
*   `sqlite_atomic_fetch_sub_8 / 16 / 32 / 64(ptr, val)`: Subtracts `val` and returns the *old* value.

### 5. Bitwise Operations (Returns old value)
*   `sqlite_atomic_fetch_and_8 / 16 / 32 / 64(ptr, val)`
*   `sqlite_atomic_fetch_or_8 / 16 / 32 / 64(ptr, val)`
*   `sqlite_atomic_fetch_xor_8 / 16 / 32 / 64(ptr, val)`

## C++ API Reference (`sqlite3_atomic.hpp`)

If you are compiling with a C++ compiler (`g++`, `clang++`, MSVC), you can include `sqlite3_atomic.hpp` instead. This provides a fully polymorphic API identical to `<atomic>`, automatically detecting the bit-width of your variables at compile time.

You no longer need to append suffixes like `_32` or `_ptr`. The template safely handles integers, booleans, and pointers automatically:

```cpp
#include "sqlite3_atomic.hpp"

long state = 0;
void* my_ptr = nullptr;
bool is_ready = false;

// Automatically routes to sqlite_atomic_store_32
sqlite_atomic_store(&state, 100L); 

// Automatically routes to sqlite_atomic_store_ptr
sqlite_atomic_store(&my_ptr, (void*)0x1234); 

// Automatically routes to sqlite_atomic_store_8
sqlite_atomic_store(&is_ready, true); 

long old = sqlite_atomic_fetch_add(&state, 50L);
```
