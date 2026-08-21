# TinyLock Architecture

The design goal behind `sqlite3_tiny_lock` was to create a locking primitive that could safely synchronize threads in a high-concurrency SQLite extension, without requiring the C++ Standard Library (`<atomic>`) or OS-heavy primitives (`sqlite3_mutex_alloc`).

## The `sqlite3_atomic.h` Primitives

C standard libraries lack cross-platform atomics prior to C11 (and even then, support is patchy across older compilers). To guarantee atomic lock acquisition, `sqlite3_tiny_lock` imports `sqlite3_atomic.h`. 

This dedicated atomic header wraps compiler-specific built-ins (GCC's `__atomic` and MSVC's `_Interlocked`) to provide perfectly sized `SQLITE_ATOMIC_CAS_WEAK_32` and `SQLITE_ATOMIC_STORE_32` macros. For a deep-dive on how the atomics work, see the [Atomic Architecture](ATOMIC_ARCHITECTURE.md) document.

## The "Hybrid" Nature of the Lock

A classic "Spinlock" loops continuously checking a boolean flag. While fast, a naive spinlock consumes 100% of a CPU core, generates massive heat, and starves other threads trying to do work on the same core. `TinyLock` uses a dual-architecture hybrid approach to solve this.

### 1. Physical Hardware (x86, ARM, RISC-V) -> CPU Yielding
When compiled natively, `TinyLock` operates as a highly optimized spinlock. Inside the spin loop, it executes `SQLITE_CPU_RELAX()`.

Because CPU relaxation is a spinlock mechanic (not a raw atomic memory operation), these macros are defined directly inside `sqlite3_tiny_lock.h` rather than the atomics header.

Depending on the architecture, this macro triggers a specific hardware instruction:
- **x86/x64**: `pause` (or `_mm_pause()` on MSVC)
- **ARM**: `yield`
- **RISC-V**: `pause`

These instructions tell the physical CPU processor: *"This thread is in a spin-wait loop. Temporarily throttle the pipeline and give execution priority to the other threads sharing this core."* It keeps the lock acquisition blistering fast (staying in user-space without an OS context switch) while preventing CPU starvation.

### 2. WebAssembly (Wasm) -> Sleeping Mutex
WebAssembly cannot execute tight spin-loops in a browser environment without freezing the host tab or triggering watchdog timeouts.

When compiled to Wasm (`#if defined(__wasm__)`), `TinyLock` dynamically transforms into a true OS-level sleeping mutex.
- During `lock()`, if the memory is already locked, it calls `__builtin_wasm_memory_atomic_wait32`. This suspends the Wasm thread completely (0% CPU usage).
- During `unlock()`, it calls `__builtin_wasm_memory_atomic_notify`, which signals the Wasm engine to wake up the sleeping thread.

## Memory Footprint

Because `TinyLock` only needs to track a single integer state (0 = Unlocked, 1 = Locked), the entire struct is exactly **4 bytes**.

```c
typedef struct {
    int state;
} sqlite3_tiny_lock;
```

This microscopic size allows it to be embedded directly by value into other structs (like the `SqlitePtrControlBlock`), bypassing heap fragmentation and avoiding the `malloc`/`free` lifecycle entirely.
