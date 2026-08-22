# TinyLock Architecture

The design goal behind `sqlite3_tiny_lock` was to create a locking primitive that could safely synchronize threads in a high-concurrency SQLite extension, without requiring the C++ Standard Library (`<atomic>`) or OS-heavy primitives (`sqlite3_mutex_alloc`).

## The `sqlite3_atomic.h` Primitives

C standard libraries lack cross-platform atomics prior to C11 (and even then, support is patchy across older compilers). To guarantee atomic lock acquisition, `sqlite3_tiny_lock` imports `sqlite3_atomic.h`. 

This dedicated atomic header wraps compiler-specific built-ins (GCC's `__atomic` and MSVC's `_Interlocked`) to provide perfectly sized `sqlite_atomic_cas_weak_32`, `sqlite_atomic_store_32`, and `sqlite_atomic_load_32` primitives. For a deep-dive on how the atomics work, see the [Atomic Architecture](ATOMIC_ARCHITECTURE.md) document.

## The "Hybrid" Nature of the Lock

A classic "Spinlock" loops continuously checking a boolean flag. While fast, a naive spinlock consumes 100% of a CPU core, generates massive heat, and starves other threads trying to do work on the same core. `TinyLock` uses a dual-architecture hybrid approach to solve this.

### 1. Physical Hardware (x86, ARM, RISC-V) -> Cache-Friendly TTAS Spinlock
When compiled natively, `TinyLock` operates as a highly optimized **Test and Test-And-Set (TTAS)** spinlock. 

If multiple threads simply hammer `CAS` (Compare-And-Swap) or `Exchange` in a tight loop, they cause a MESI protocol "cache bouncing" storm, effectively jamming the motherboard's memory bus as every core fights for exclusive write access to the cache line.
To prevent this, `TinyLock` uses an outer `CAS_WEAK` (Test-And-Set) loop, but an inner `LOAD` (Test) loop. The inner loop spins on a pure, read-only memory operation. This allows the cache line to remain in a "Shared" state across all CPU cores, keeping the memory bus perfectly quiet until the lock is actually released.

Inside this inner spin loop, it executes `SQLITE_CPU_RELAX()`. Because CPU relaxation is a spinlock mechanic (not a raw atomic memory operation), these macros are defined directly inside `sqlite3_tiny_lock.h` rather than the atomics header.

Depending on the architecture, this macro triggers a specific hardware instruction:
- **x86/x64**: `pause` (or `_mm_pause()` on MSVC)
- **ARM**: `yield`
- **RISC-V**: `pause`

These instructions tell the physical CPU processor: *"This thread is in a spin-wait loop. Temporarily throttle the pipeline and give execution priority to the other threads sharing this core."* It keeps the lock acquisition blistering fast (staying in user-space without an OS context switch) while preventing CPU starvation.

### 2. WebAssembly (Wasm) -> Sleeping Mutex
WebAssembly cannot execute tight spin-loops in a browser environment without freezing the host tab or triggering watchdog timeouts.

When compiled to Wasm (`#if defined(__wasm__)`), `TinyLock` dynamically transforms into a true OS-level sleeping mutex. This is achieved by encapsulating Wasm intrinsics directly into the CPU macros:
- **`SQLITE_CPU_RELAX(ptr)`**: Instead of a CPU pause instruction, this macro intercepts the lock pointer and calls `__builtin_wasm_memory_atomic_wait32`. This suspends the Wasm thread completely (0% CPU usage) until the memory changes.
- **`SQLITE_CPU_NOTIFY(ptr)`**: Called during `unlock()`. On native CPUs, this is a no-op. On Wasm, it calls `__builtin_wasm_memory_atomic_notify`, which signals the Wasm engine to wake up the sleeping thread.

## Memory Footprint

Because `TinyLock` only needs to track a single boolean state (0 = Unlocked, 1 = Locked), the struct dynamically sizes itself based on the target architecture to minimize overhead:

- **Native (x86, ARM, RISC-V)**: Exactly **1 byte** (`char state;`).
- **WebAssembly**: Exactly **4 bytes** (`int state;`). Wasm's `memory.atomic.wait32` instruction strictly requires a 32-bit aligned memory address.

This microscopic size allows it to be embedded directly by value into other structs (like the `SqlitePtrControlBlock`), bypassing heap fragmentation and avoiding the `malloc`/`free` lifecycle entirely.
