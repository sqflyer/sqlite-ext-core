# Systems Architecture: Freestanding Coroutines & Fibers (`sqlite3_coro.h` / `sqlite3_coro.hpp`)

This document provides a comprehensive systems-level engineering specification of the **Freestanding Coroutine & Fiber Subsystem**. It details **CPU register preservation**, **native Win32 Fiber integration**, **POSIX `ucontext_t` stack layout**, **stable heap control block design**, and **freestanding C++20 compiler state-machine lowering without `<coroutine>`**.

> **User Guide & Tutorials**: For API references, quickstarts, and code examples, see [`docs/COROUTINE_README.md`](COROUTINE_README.md).

---

## 1. Executive Summary & Design Invariants

In high-concurrency database extensions, traditional operating system threads present severe architectural bottlenecks:
1. **Memory Overhead**: Each OS thread consumes 1MB to 8MB of virtual memory for its call stack.
2. **Context Switching Latency**: Switching OS threads forces a kernel trap, page table checks, and CPU pipeline flushes (~1,000–5,000 ns).
3. **Control Inversion in Virtual Tables**: SQLite's step-by-step cursor API (`xNext`, `xColumn`) forces developers to build manual state machines.

`sqlite-ext-core` provides two complementary user-space cooperative execution models:
- **Stackful Fibers (Pure C99 & C++11)**: Dedicated small stacks (16KB–64KB) allocated via `sqlite3_malloc64` enabling `yield` from arbitrary call depths.
- **Stackless Generators (C++20 `co_yield`)**: Zero-stack compiler-lowered state machines (~48-byte frames) running in ~1–3 nanoseconds.

---

## 2. Core Architectural Model & State Machine

Every coroutine transitions through a deterministic state lifecycle:

```
                      ┌─────────────────────────┐
                      │      UNINITIALIZED      │ (state == nullptr)
                      └────────────┬────────────┘
                                   │ sqlite3_coro_create()
                                   ▼
                      ┌─────────────────────────┐
                      │          READY          │ (is_done = 0, is_running = 0)
                      └────────────┬────────────┘
                                   │
                    ┌──────────────┼──────────────┐
                    │ resume()     │              │ destroy()
                    ▼              │              ▼
       ┌────────────────────────┐  │  ┌─────────────────────────┐
 ┌────►│        RUNNING         │  │  │        DESTROYED        │
 │     │ (is_running = 1)       │  │  │ (memory freed, NULL)    │
 │     └───────────┬────────────┘  │  └─────────────────────────┘
 │                 │               │              ▲
 │ yield()         │ return / end  │              │
 │                 ▼               │              │
 │     ┌────────────────────────┐  │              │
 └─────┤       SUSPENDED        │  │              │
       │ (is_running = 0)       │  │              │
       └───────────┬────────────┘  │              │
                   │               │              │
                   ▼               │              │
       ┌────────────────────────┐  │              │
       │          DONE          ├──┴──────────────┘
       │ (is_done = 1)          │
       └────────────────────────┘
```

---

## 3. Hardware CPU Context Switching & Register Preservation

When a stackful fiber switches execution context, it preserves all **callee-saved CPU registers** according to the platform ABI:

```
                             REGISTER CONTEXT SWAPPING (x86_64)
      Caller Execution Context                             Coroutine Execution Context
 ┌────────────────────────────────┐                   ┌────────────────────────────────┐
 │ Instruction Pointer (RIP)      │                   │ Instruction Pointer (RIP)      │
 │ Stack Pointer (RSP)            │  SwitchToFiber()  │ Stack Pointer (RSP)            │
 │ Base Pointer (RBP)             │ ────────────────► │ Base Pointer (RBP)             │
 │ Callee-Saved: RBX, R12-R15     │   swapcontext()   │ Callee-Saved: RBX, R12-R15     │
 │ XMM / SSE Control State        │ ◄──────────────── │ XMM / SSE Control State        │
 └────────────────────────────────┘      yield()      └────────────────────────────────┘
```

### 1. Windows Native Fibers (Win32 API)
Windows provides native hardware-accelerated user-mode fibers in `kernel32.dll`:
- **`ConvertThreadToFiber(NULL)`**: Converts the calling OS thread into a master fiber, establishing a valid fiber execution context.
- **`CreateFiber(stack_size, trampoline, st)`**: Allocates a user-mode execution stack and initializes fiber control records.
- **`SwitchToFiber(handle)`**: Swaps hardware registers (RIP, RSP, RBP, RBX, R12–R15, XMM6–XMM15) in **~15–20 CPU cycles** without entering the Windows NT kernel.
- **`DeleteFiber(handle)`**: Releases the committed virtual memory pages back to the operating system.

### 2. POSIX Context Switching (`ucontext_t`)
On Linux, macOS, and BSD platforms:
- **Stack Memory**: Explicitly allocated via `sqlite3_malloc64(stack_size)`.
- **`makecontext(&ctx, trampoline, 2, hi, lo)`**: Initializes the `ucontext_t` structure with pointer arguments split across 32-bit registers for ABI compatibility.
- **`swapcontext(&caller_ctx, &coro_ctx)`**: Atomically saves the caller's CPU registers and activates the coroutine's stack and instruction pointer.
- **Teardown**: Releasing stack memory routes strictly through `sqlite3_free()`.

---

## 4. Stable Control Block Design (1-Cycle Move Safety)

In C++, moving a coroutine instance (`SqliteCoroutine c2 = sqlite_move(c1)`) must not invalidate internal callbacks or execution pointers.

`sqlite3_coro.h` decouples the handle from the state via a **stable heap control block**:

```cpp
typedef struct sqlite3_coro_state {
    void*                fiber_handle;   /**< OS fiber / context handle */
    void*                caller_fiber;   /**< Caller's return context */
    sqlite3_coro_entry_t entry_fn;       /**< User entry point function */
    void*                arg;            /**< User argument pointer */
    void*                yield_value;    /**< Data pointer passed across boundary */
    int                  is_done;        /**< 1 if finished, 0 otherwise */
    int                  is_running;     /**< 1 if active, 0 otherwise */
} sqlite3_coro_state_t;

typedef struct sqlite3_coro {
    sqlite3_coro_state_t* state;         /**< Stable 8-byte heap pointer */
} sqlite3_coro_t;
```

### Architectural Guarantees:
1. **Pointer Invariance**: The OS fiber trampoline always receives the stable `sqlite3_coro_state_t*` pointer. Moving `SqliteCoroutine` simply copies the 8-byte pointer and clears the source.
2. **Immediate State Reflection**: When a coroutine entry function completes, setting `state->is_done = 1` immediately reflects across any moved wrapper handles pointing to that state block.
3. **100% SQLite Memory Profiling**: The control block is allocated via `sqlite3_malloc64(sizeof(sqlite3_coro_state_t))` and freed via `sqlite3_free()`.

---

## 5. Non-Virtual Closure Trampoline Engine

Standard C++ `std::function` relies on virtual table dispatches, RTTI, and global `operator new`/`delete` symbols, which fail under `-nostdlib++ -fno-rtti -fno-exceptions`.

`SqliteCoroutine` executes capturing lambdas via **static function-pointer trampolines**:

```cpp
struct CallableHolderBase {
    void (*invoke_fn)(CallableHolderBase*);
    void (*destroy_fn)(CallableHolderBase*);
};

template <typename F>
struct CallableHolder : public CallableHolderBase {
    F func;
    CallableHolder(F&& f) : func(sqlite_forward<F>(f)) {
        this->invoke_fn = &invoke_impl;
        this->destroy_fn = &destroy_impl;
    }
    static void invoke_impl(CallableHolderBase* self) {
        static_cast<CallableHolder<F>*>(self)->func();
    }
    static void destroy_impl(CallableHolderBase* self) {
        sqlite_delete(static_cast<CallableHolder<F>*>(self));
    }
};
```

### Execution Flow:
1. `SqliteCoroutine(lambda)` allocates `CallableHolder<Lambda>` using `sqlite_new` (`sqlite3_malloc64`).
2. The static C trampoline invokes `holder->invoke_fn()`.
3. When the coroutine finishes, `holder->destroy_fn()` automatically calls `sqlite_delete` (`sqlite3_free`).
4. **Result**: Zero virtual method tables (`vtable`), zero RTTI overhead, and zero memory leaks.

---

## 6. Freestanding C++20 Stackless Lowering (`co_yield`)

When compiled with a C++20 compiler (`-std=c++20` or `/std:c++20`), `sqlite3_coro.hpp` declares forward traits in namespace `std` without `#include <coroutine>`:

```cpp
namespace std {
    template <typename ReturnType, typename... Args>
    struct coroutine_traits;

    template <typename Promise = void>
    struct coroutine_handle;

    struct suspend_always {
        constexpr bool await_ready() const noexcept { return false; }
        constexpr void await_suspend(coroutine_handle<>) const noexcept {}
        constexpr void await_resume() const noexcept {}
    };
}
```

### Frame Memory Management:
The generator's `promise_type` overloads `operator new` and `operator delete` to ensure the compiler-generated state-machine frame is allocated strictly through SQLite's memory manager:

```cpp
struct promise_type {
    void* operator new(size_t sz) {
        return sqlite3_malloc64(static_cast<sqlite3_uint64>(sz));
    }
    void operator delete(void* ptr) {
        sqlite3_free(ptr);
    }
    // ...
};
```

### Stackful Fibers vs C++20 Stackless Architectural Comparison:

```
┌──────────────────────────────────────┬──────────────────────────────────────┐
│       STACKFUL FIBER COROUTINE       │       STACKLESS C++20 COROUTINE      │
├──────────────────────────────────────┼──────────────────────────────────────┤
│ • Allocated Stack: 16 KB – 64 KB     │ • Allocated Frame: ~32 – 64 Bytes    │
│ • Can yield from any recursive depth │ • Can only yield in top-level func   │
│ • Context switch: ~15 – 25 ns        │ • Context switch: ~1 – 3 ns          │
│ • Pure C99 & C++11 compatible        │ • Requires C++20 compiler support    │
│ • Ideal for deep recursive AST/trees │ • Ideal for 100,000+ linear tasks    │
└──────────────────────────────────────┴──────────────────────────────────────┘
```

### Architectural Selection Criteria:
1. **Call Stack Preservation**:
   - **Stackful Fibers**: Preserve the entire hardware call stack across yields. When a fiber calls `traverse_node()` $\to$ `parse_element()` $\to$ `yield()`, the intermediate activation records and local variables on the fiber's stack remain fully intact.
   - **Stackless Coroutines**: The compiler transforms only the top-level function into a heap-allocated state frame. Calling another function and attempting to yield from inside it is impossible without passing coroutine handles manually through every layer of the call chain.
2. **Memory Scaling & Density**:
   - For workloads requiring hundreds of thousands to millions of concurrent tasks (e.g. multi-tenant cron queues or high-throughput message channels), stackless coroutines achieve orders-of-magnitude higher memory density (~48 MB for 1,000,000 active frames vs ~16 GB for 1,000,000 fiber stacks).
3. **Compiler & Platform Portability**:
   - Stackful fibers run on pure C99/C11 and C++11 compilers without language extensions.
   - Stackless coroutines require compiler support for C++20 coroutine lowering.

---

## 7. Application in $N \times M$ Multi-Database Cron Schedulers

Coroutines enable building high-density asynchronous cron engines where **$N$ worker OS threads** service **$M$ loaded databases** running **$K$ thousands of scheduled tasks**:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                             N OS WORKER THREADS                             │
│    [Thread 1]           [Thread 2]           [Thread 3]        [Thread N]   │
│  (SqliteThread)       (SqliteThread)       (SqliteThread)    (SqliteThread) |
└──────────┬────────────────────┬────────────────────┬─────────────────┬──────┘
           │                    │                    │                 │
           ▼                    ▼                    ▼                 ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                  GLOBAL CRON WHEEL / COOPERATIVE RUN QUEUE                  │
│                Priority Queue sorted by next_run_monotonic_ms               │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │ Pop & resume ready task
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                    M LOADED DATABASES (COROUTINE FIBER JOBS)                │
│                                                                             │
│   Database 1 (App DB)          Database 2 (Metrics)      Database M (Logs)  │
│   • Coro A: Token Purge        • Coro C: Flush WAL       • Coro E: Archive  │
│   • Coro B: VACUUM Chunk       • Coro D: Rollup Daily    • Coro F: Reindex  │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Architectural Benefits for Schedulers:
1. **Zero Lock Starvation**: Long batch operations (e.g. archiving 100,000 rows) yield every 500 rows, allowing reader/writer transactions on other databases to execute without delay.
2. **Non-Blocking Sleep**: Pausing a task for 60 seconds suspends its coroutine and re-queues its deadline, leaving all $N$ OS worker threads free to execute other database tasks.
3. **Per-Database Isolation**: Each database state is sandboxed via `SqliteExtState<T>`, ensuring clean cancellation of only the affected coroutines when a database connection closes.

---

## 8. Systems Comparison with Industry Coroutine Runtimes

| Systems Metric | Boost.Context (`fcontext`) | Standard C++20 (`<coroutine>`) | Tencent `libco` | `sqlite-ext-core` Coroutine |
| :--- | :--- | :--- | :--- | :--- |
| **Context Engine** | Custom handwritten assembly | Compiler-lowered frame | Custom x86 assembly | **Win32 Fibers / POSIX `ucontext_t`** |
| **Memory Accounting** | Untracked global malloc | Global operator new | Untracked glibc heap | **100% `sqlite3_malloc64`** |
| **Windows Native Fibers** | No (Bypasses Win32 TEB) | N/A (Stackless frame) | No (Unsupported) | **Native `ConvertThreadToFiber` / `CreateFiber`** |
| **POSIX Conformance** | Direct register jumps | Frame pointer allocation | Non-standard asm hacks | **Standard POSIX `ucontext_t`** |
| **Stack-Switch Safety** | SEH / ASan stack breaks | Stackless (Frame only) | Breaks on non-x86 ABIs | **Win32 Fiber Lock & ASan safe** |
| **Freestanding Support** | Requires Boost libraries | Requires `<coroutine>` CRT | Requires libc runtime | **100% `-nostdlib++` Safe** |

### Deep Systems Rationale & Trade-off Analysis:

1. **Why Win32 Fibers on Windows instead of handwritten assembly (`fcontext`)?**
   - *Operating System TEB/SEH Integrity*: Windows Structured Exception Handling (SEH) and Thread Environment Block (TEB) records track active stack boundaries. Bypassing Windows fiber APIs with raw assembly jumps (like `fcontext`) can cause `ntdll!RtlVirtualUnwind2` crashes during stack unwinding. Native Win32 Fibers update the OS kernel's fiber control structures cleanly.
2. **Why POSIX `ucontext_t` on Linux/macOS?**
   - *ABI & Signal Safety*: `ucontext_t` cleanly preserves platform-specific signal masks, floating-point control words, and callee-saved register state according to the System V AMD64 and ARM64 ABIs without brittle inline assembly.
3. **Why Custom C++20 Coroutine Traits without `<coroutine>`?**
   - *Eliminating Standard Library Linkage*: Standard `<coroutine>` pulls in C++ runtime symbols (`__cxa_allocate_exception`, standard allocators). `sqlite-ext-core` defines custom freestanding `std::coroutine_handle` and `std::coroutine_traits` specializations that lower `co_yield` expressions directly onto `sqlite3_malloc64` frames.

---

## 9. Freestanding Memory Guarantees (`-nostdlib++`)

- **Zero Standard Library Dependencies**: Fully independent of `libstdc++`, `libc++`, and MSVC CRT dynamic DLLs.
- **Zero Memory Leaks**: Control blocks and stack buffers are freed deterministically upon coroutine destruction.
- **Exception Freedom**: Complies with `-fno-exceptions`; allocation failures produce deterministic `SQLITE_NOMEM` / `SQLITE_MISUSE` error codes verified via `.is_valid()`.
