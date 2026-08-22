# Atomic Architecture

The `sqlite3_atomic.h` library achieves 100% cross-platform atomic memory synchronization by directly wrapping compiler-specific backend intrinsics, bypassing standard libraries entirely.

## MSVC vs GCC/Clang Polymorphism

One of the largest architectural challenges in writing cross-platform C atomics is dealing with how Microsoft Visual Studio (MSVC) handles memory sizes compared to GCC/Clang.

### The GCC/Clang Approach (Polymorphism)
GCC and Clang provide a single set of built-in atomic functions, primarily `__atomic_compare_exchange_n` and `__atomic_store_n`. 

These built-ins are **polymorphic**. The compiler looks at the data type of the pointer passed into the function during compilation. If you pass an `int8_t*`, the compiler automatically generates a 1-byte atomic instruction. If you pass a `uint64_t*`, it generates an 8-byte atomic instruction.

### The MSVC Approach (Strict Typing)
Microsoft's `_Interlocked*` intrinsics are **not** polymorphic. They are strictly typed C macros.
- If you use `_InterlockedExchange`, it expects exactly a 32-bit `long`. 
- If you pass a 64-bit integer into a 32-bit `_Interlocked` macro, MSVC will successfully compile it, but it will physically truncate the memory swap, corrupting your data.

### The Architectural Solution
To create a safe, unified API across both compilers, `sqlite3_atomic.h` forces the developer to explicitly declare the bit-width of their atomic operation using suffixes (`_8`, `_16`, `_32`, `_64`, `_PTR`).

On MSVC, these explicitly map to the safely sized `_Interlocked` variants (e.g., `_InterlockedExchange8`, `_InterlockedExchange16`).
On GCC/Clang, they all map to the same polymorphic `__atomic_store_n` built-in, satisfying the compiler while providing API parity with MSVC.

## Weak vs Strong CAS

The library provides both `WEAK` and `STRONG` Compare-And-Swap macros.

1. **Weak CAS**: May fail "spuriously" on some architectures (like ARM) even if the memory matches the expected value. Because it doesn't have to guarantee a non-spurious failure, the CPU can execute it faster. It should **only** be used inside a `while` loop (which naturally handles spurious failures by just trying again).
2. **Strong CAS**: Guaranteed to never fail spuriously. It is slightly slower on ARM because it implements an internal retry loop at the hardware level. It should be used when you only have one single chance to acquire the memory (no `while` loop).

### The MSVC Exception
x86 and x64 hardware (which MSVC primarily targets) do not suffer from spurious CAS failures at the silicon level. Their `LOCK CMPXCHG` instructions are inherently "Strong".

Because of this, MSVC's `_InterlockedCompareExchange` does not differentiate between Weak and Strong. In `sqlite3_atomic.h`, both `sqlite_atomic_cas_weak` and `sqlite_atomic_cas_strong` safely map to the exact same MSVC intrinsic.

## The MSVC TOCTOU CAS Trap
Standard C++ atomics require that if a CAS fails, the `expected` variable is automatically updated with the actual value currently in memory. 

If this was implemented as a pure macro (e.g., `*expected = *ptr`), a thread could fail the CAS, but before the macro executed `*expected = *ptr`, another thread could change `*ptr`. This creates a Time-Of-Check to Time-Of-Use (TOCTOU) race condition, loading incorrect state and breaking spinloops.

To solve this, `sqlite3_atomic.h` uses `static inline` functions (`__sqlite_atomic_cas_32`) for MSVC that safely capture the exact return value of `_InterlockedCompareExchange` and write it to `expected` atomically.

## Hardware-Accelerated 8-Bit Arithmetic
When implementing 8-bit increments (`sqlite_atomic_increment_8`), a naive implementation might use a CAS spinloop. However, MSVC provides `_InterlockedExchangeAdd8`, which allows the hardware to bypass the spinloop entirely and execute the addition directly in the CPU pipeline. `sqlite3_atomic.h` leverages this to provide lock-free progress guarantees even under extreme thread contention.

## Pointer Loading on MSVC
Unlike integers which can be atomically loaded using a bitwise identity operation like `_InterlockedOr(ptr, 0)`, pointers cannot be bitwise-OR'd in C/C++. To achieve a thread-safe, atomic pointer load with full memory barriers on MSVC, `sqlite3_atomic.h` leverages `_InterlockedCompareExchangePointer(ptr, (void*)0, (void*)0)`. This cleanly compares the pointer to `NULL`, harmlessly swaps it with `NULL` if it was already `NULL`, and atomically returns the exact pointer value.

## C++ Zero-Dependency Polymorphism (`sqlite3_atomic.hpp`)
While C developers must explicitly call `sqlite_atomic_store_32`, modern C++ developers expect standard polymorphic overloads (`sqlite_atomic_store(ptr, val)`). However, SQLite extensions built in `no-std` environments cannot link against `<type_traits>` to use `std::enable_if`.

To solve this, `sqlite3_atomic.hpp` implements a custom, zero-dependency **SFINAE (Substitution Failure Is Not An Error)** engine. 
1. It defines a manual `sqlite_is_pointer<T>` and `sqlite_enable_if`.
2. When a C++ developer calls `sqlite_atomic_store(&my_var, 5)`, the SFINAE engine intercepts the call.
3. If `my_var` is a pointer, it routes instantly to the underlying `_ptr` C macro.
4. If `my_var` is a primitive (integer/bool), it calculates `sizeof(my_var)` and routes the request to a highly optimized struct template (`SqliteAtomicOps<Size>`), bridging the type directly to the correct `_8`, `_16`, `_32`, or `_64` C macro.

This provides the exact developer experience of `<atomic>` with absolutely zero standard library overhead.
