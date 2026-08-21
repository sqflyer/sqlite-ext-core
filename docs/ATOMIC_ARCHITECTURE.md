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

Because of this, MSVC's `_InterlockedCompareExchange` does not differentiate between Weak and Strong. In `sqlite3_atomic.h`, both `SQLITE_ATOMIC_CAS_WEAK` and `SQLITE_ATOMIC_CAS_STRONG` safely map to the exact same MSVC intrinsic.
