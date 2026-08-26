# Threading & Synchronization Architecture (`sqlite3_thread.h` / `sqlite3_thread.hpp`)

This document details the systems-level architectural design of the **Freestanding Threading Subsystem**, focusing on **zero-dependency closure execution**, **non-virtual static function pointer trampolines**, **operating system kernel primitive translation (Win32 vs POSIX)**, and **memory safety guarantees under `-nostdlib++`**.

> **API & Usage Guide**: For tutorials, examples, and the public API reference, see [`docs/THREAD_README.md`](THREAD_README.md).

---

## 1. Architectural Motivation: Freestanding Concurrency

SQLite extensions that implement background tasks, write-ahead logging synchronization, or async replication cannot use standard `<thread>`, `<mutex>`, or `<condition_variable>` headers when compiled with `-nostdlib++` or `/NODEFAULTLIB`.

The core architectural requirements for the threading subsystem are:
1. **Zero Standard Library Dependencies**: Complete elimination of `libstdc++`, `libc++`, and MSVC runtime dependencies.
2. **Lambda & Closure Support**: Ability to pass capturing C++11 lambdas directly to threads without virtual function tables (`vtable`) or dynamic casting (`rtti`).
3. **SQLite Allocator Integration**: Any dynamic memory used to package closure arguments must route through `sqlite_new` and `sqlite_delete` (`sqlite3_malloc` / `sqlite3_free`).
4. **Native OS Concurrency**: Direct mapping to low-level operating system APIs (Win32 threads/events/condition variables on Windows, and POSIX `pthread` on Linux/macOS).

---

## 2. Kernel Primitives & OS Abstraction Model

```
                   ┌──────────────────────────────────────────────┐
                   │                 SqliteThread                 │
                   │   - std::thread semantics & move lifecycle   │
                   │   - Stateless/Stateful Lambda Support        │
                   │   - Zero C++ runtime overhead (-nostdlib++)  │
                   └──────────────────────┬───────────────────────┘
                                          │
                                          ▼
                   ┌──────────────────────────────────────────────┐
                   │          sqlite3_thread_create / join        │
                   └──────────────┬────────────────┬──────────────┘
                                  │                │
                 ┌────────────────┴───┐        ┌───┴────────────────┐
                 ▼                    ▼        ▼                    ▼
         ┌───────────────┐    ┌───────────────┐ ┌───────────────┐   ┌───────────────┐
         │ CreateThread  │    │ WaitForSingle │ │ pthread_create│   │ pthread_join  │
         │   (Win32)     │    │ Object (Win32)│ │    (POSIX)    │   │    (POSIX)    │
         └───────────────┘    └───────────────┘ └───────────────┘   └───────────────┘
```

### OS Primitive Mapping Table

| Subsystem Component | Windows (Win32 API) | POSIX (Linux / macOS) |
| :--- | :--- | :--- |
| **Thread Handle** | `HANDLE` via `CreateThread` | `pthread_t` via `pthread_create` |
| **Thread Joining** | `WaitForSingleObject` + `CloseHandle` | `pthread_join` |
| **Thread Detachment** | `CloseHandle` (leaves thread running) | `pthread_detach` |
| **Thread Yield** | `SwitchToThread()` / `Sleep(0)` | `sched_yield()` / `pthread_yield()` |
| **Condition Variable** | `CONDITION_VARIABLE` | `pthread_cond_t` |
| **Condition Wait** | `SleepConditionVariableCS` | `pthread_cond_wait` / `pthread_cond_timedwait` |
| **Condition Signal** | `WakeConditionVariable` / `WakeAll` | `pthread_cond_signal` / `pthread_cond_broadcast` |
| **Native Mutex** | `CRITICAL_SECTION` | `pthread_mutex_t` |

---

## 3. Non-Virtual Closure Trampoline Engine

Standard C++ `std::thread` uses internal virtual base classes (`std::__thread_run`) to type-erase arbitrary callable objects. In a `-nostdlib++ -fno-rtti` build, this causes missing symbol errors for `vtable`, `typeinfo`, and `operator delete`.

`SqliteThread` solves this using a **static function-pointer trampoline pair**:

```cpp
struct CallableHolderBase {
    void (*invoke_fn)(CallableHolderBase*);
    void (*destroy_fn)(CallableHolderBase*);
};

template <typename F>
struct CallableHolder : public CallableHolderBase {
    F func;
    CallableHolder(F&& f) : func(static_cast<F&&>(f)) {
        invoke_fn = &invoke_impl;
        destroy_fn = &destroy_impl;
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
1. When `SqliteThread(callable)` is instantiated, it allocates a `CallableHolder<F>` via `sqlite_new<CallableHolder<F>>(sqlite_move(f))`.
2. The pointer is passed as the `void*` argument to `sqlite3_thread_create`.
3. The static C trampoline `sqlite_thread_trampoline(void* arg)` executes:
   ```cpp
   CallableHolderBase* holder = static_cast<CallableHolderBase*>(arg);
   holder->invoke_fn(holder);   // Executes lambda
   holder->destroy_fn(holder);  // Automatically frees holder via sqlite_delete
   ```
4. **Result**: Zero dynamic dispatch overhead, zero standard library dependencies, and zero memory leaks.

---

## 4. Condition Variables & Predicate Synchronization

`SqliteConditionVariable` provides robust predicate synchronization to prevent race conditions and spurious wakeups:

```cpp
template <typename Predicate>
void wait(SqliteThreadMutexGuard& lock, Predicate pred) {
    while (!pred()) {
        wait(lock);
    }
}
```

### Timed Waits (`wait_for`):
Calculates high-resolution monotonic deadlines using `sqlite3_time.h`:
1. Converts the timeout milliseconds into an absolute deadline.
2. Loops evaluating `sqlite3_thread_cond_timedwait` while the predicate returns `false`.
3. Returns `true` if the condition was met, or `false` if the timeout elapsed.

---

## 5. Memory & Linkage Guarantees

- **No Heap Leaks**: Thread closures are destructed deterministically at the end of the thread routine even if detached.
- **OOM Resilience**: Allocation failures during closure construction safely fall back to an unstarted thread state without throwing exceptions.
- **Move-Only Safety**: `SqliteThread` enforces strict move-only semantics (`sqlite_move`), preventing double-join or duplicate handle closure bugs.
