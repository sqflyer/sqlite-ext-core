# Atomic Primitives (`sqlite3_atomic.h`)

The `sqlite3_atomic.h` header provides a zero-dependency, cross-platform suite of atomic memory operations. 

It is designed to give C and C++ SQLite extensions full thread-safety and atomic memory manipulation without relying on `<stdatomic.h>` (which has poor support on older C compilers) or `<atomic>` (which is unavailable when compiling C++ with `-nostdlib++` or `-fno-exceptions`).

## Features
- **Zero Dependencies**: Requires absolutely no OS headers or standard library files.
- **Cross-Platform**: Natively detects and optimizes for MSVC (Windows), GCC, and Clang (macOS, Linux, iOS, Android, WebAssembly).
- **Strongly Typed**: Provides explicitly sized operations for 8-bit, 16-bit, 32-bit, 64-bit, and Pointer types to prevent accidental memory truncation on strict compilers.

## API Reference

The library provides two primary atomic operations, sized for your specific data types.

### 1. Atomic Store (Release Semantics)
Atomically overwrites a memory address with a new value. Other threads will never see a partially-written value.

*   `SQLITE_ATOMIC_STORE_8(ptr, val)`
*   `SQLITE_ATOMIC_STORE_16(ptr, val)`
*   `SQLITE_ATOMIC_STORE_32(ptr, val)`
*   `SQLITE_ATOMIC_STORE_64(ptr, val)`
*   `SQLITE_ATOMIC_STORE_PTR(ptr, val)`

```c
#include "sqlite3_atomic.h"

int32_t my_counter = 0;

void reset_counter() {
    SQLITE_ATOMIC_STORE_32(&my_counter, 0);
}
```

### 2. Compare-And-Swap (CAS)
Atomically compares the current value in memory (`ptr`) with an `expected` value. If they match, it replaces it with the `desired` value and returns `1` (Success). If they do not match, it updates the `expected` variable with the *actual* value currently in memory and returns `0` (Failure).

**Weak CAS** (Best for loops):
*   `SQLITE_ATOMIC_CAS_WEAK_8 / 16 / 32 / 64 / PTR`

**Strong CAS** (Best for single-try attempts):
*   `SQLITE_ATOMIC_CAS_STRONG_8 / 16 / 32 / 64 / PTR`

```c
#include "sqlite3_atomic.h"

int32_t state = 0; // 0 = Unlocked, 1 = Locked

void lock() {
    int expected = 0;
    // Keep trying to swap 0 for 1 until successful
    while (!SQLITE_ATOMIC_CAS_WEAK_32(&state, &expected, 1)) {
        expected = 0; // Reset for the next attempt
    }
}
```
