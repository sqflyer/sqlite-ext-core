# Freestanding Threading & Synchronization Subsystem (`sqlite3_thread.h` / `sqlite3_thread.hpp`)

High-performance, zero-dependency, freestanding C and C++11 threading primitives, condition variables, and native OS mutexes. Engineered specifically for SQLite extension authors to enable **background worker loops**, **asynchronous write-behind persistence**, **time-sliced cron tasks**, and **multi-threaded pipeline coordination** without pulling in standard library runtimes.

> **Architecture Reference**: For an in-depth systems breakdown of the non-virtual closure trampolines, zero-allocation function pointers, native OS handle mappings (Win32 `CreateThread` / POSIX `pthread_create`), and memory guarantees, see [`docs/THREAD_ARCHITECTURE.md`](THREAD_ARCHITECTURE.md).

---

## 1. Architectural Philosophy: Freestanding Async Execution

SQLite extensions frequently require asynchronous processing:
1. **Background Cache Eviction**: Periodic purge passes over LRU/TTL caches without stalling VDBE query threads.
2. **Write-Behind Persistence**: Asynchronously flushing in-memory virtual table ring buffers to disk or WAL files.
3. **Telemetry & Heartbeats**: Streaming query execution metrics and background health checks.

In a strict **`-nostdlib++` / `-fno-exceptions` / `/NODEFAULTLIB`** environment, standard headers (`<thread>`, `<condition_variable>`, `<mutex>`) cannot be used because they pull in heavy C++ standard library runtime dependencies.

`sqlite3_thread.h` (Pure C) and `sqlite3_thread.hpp` (C++11) resolve this by providing zero-dependency, drop-in primitives backed directly by operating system kernel primitives.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                       C++11 THREADING WRAPPERS                              │
│ (Freestanding, RAII, Move Semantics, Stateless/Stateful Lambda Support)     │
│                                                                             │
│      SqliteThread              SqliteConditionVariable   SqliteThreadMutex  │
│  [std::thread drop-in]         [std::cond_var drop-in]   [Native OS Mutex]  │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           PURE C THREADING API                              │
│ (Zero-overhead C99 function calls wrapping OS kernel primitives)            │
│                                                                             │
│      sqlite3_thread_t          sqlite3_cond_t         sqlite3_thread_mutex_t│
│  (create / join / detach) (wait / timedwait / signal) (lock / unlock)       │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │
                 ┌─────────────────────┴─────────────────────┐
                 ▼                                           ▼
┌─────────────────────────────────┐         ┌─────────────────────────────────┐
│       Windows Native (Win32)    │         │       POSIX (Linux / macOS)     │
│  CreateThread / WaitForSingle   │         │  pthread_create / pthread_join  │
│  CONDITION_VARIABLE / CS        │         │  pthread_cond_t / mutex_t       │
└─────────────────────────────────┘         └─────────────────────────────────┘
```

---

## 2. Feature Matrix

| Feature | `SqliteThread` | `SqliteConditionVariable` | `SqliteThreadMutex` | Pure C API (`sqlite3_thread_*`) |
| :--- | :---: | :---: | :---: | :---: |
| **Standard Equivalence** | `std::thread` | `std::condition_variable` | `std::mutex` | `pthread_t` / Win32 Handles |
| **Standard Library Dep** | **0.0% (`-nostdlib++`)** | **0.0% (`-nostdlib++`)** | **0.0% (`-nostdlib++`)** | **0.0% (Pure C99)** |
| **Move Semantics** | `sqlite_move` (1-Cycle) | Non-copyable/movable | Non-copyable/movable | Pointer handle copy |
| **Closure Support** | Capturing & Stateless | Predicate evaluation | N/A | `void* (*)(void*)` + arg |
| **Timeout Support** | N/A | `wait_for(lock, ms, pred)` | N/A | `sqlite3_cond_timedwait` |
| **Destruction Policy** | Auto-join or Detach | Native OS cleanup | Native OS destroy | Explicit function calls |
| **Lock Hierarchy** | N/A | Works with Guard | Inherits `SqliteLockBase` | Direct C struct |

---

## 3. C++11 Quickstart (`sqlite3_thread.hpp`)

### 1. Spawning Threads with Closures & Lambdas
`SqliteThread` supports free function pointers, stateless lambdas, and capturing stateful closures without virtual table overhead:

```cpp
#include "async/sqlite3_thread.hpp"

// 1. Spawning with a capturing lambda
int processed_count = 0;
SqliteThread worker([&processed_count]() {
    SqliteThread::sleep_for_ms(50);
    processed_count = 1000;
});

// 2. Joining thread execution
if (worker.joinable()) {
    worker.join();
}
assert(processed_count == 1000);

// 3. Spawning with a free function
void background_task() { /* ... */ }
SqliteThread th(background_task);
th.join();
```

### 2. Move Semantics & Lifecycle Transfers
```cpp
// Threads can be cleanly transferred across scopes or containers via sqlite_move
SqliteThread th1([]() { SqliteThread::sleep_for_ms(20); });
assert(th1.joinable());

SqliteThread th2 = sqlite_move(th1); // Ownership transferred safely
assert(!th1.joinable());
assert(th2.joinable());

th2.join();
```

### 3. Background Detached Execution
```cpp
SqliteThread background_logger([]() {
    // Runs independently in background
    flush_telemetry();
});
background_logger.detach();
assert(!background_logger.joinable());
```

---

## 4. Condition Variables & Coordination (`SqliteConditionVariable`)

### 1. Producer-Consumer Coordination with Predicates
```cpp
#include "async/sqlite3_thread.hpp"

SqliteThreadMutex mutex;
SqliteConditionVariable cond;
bool job_ready = false;
int payload = 0;

// Producer Thread
SqliteThread producer([&]() {
    SqliteThread::sleep_for_ms(20);
    {
        SqliteThreadMutexGuard lock(mutex);
        payload = 42;
        job_ready = true;
    }
    cond.notify_one(); // Wake up waiting consumer
});

// Consumer Thread
{
    SqliteThreadMutexGuard lock(mutex);
    // Predicate wait protects against spurious wakeups
    cond.wait(lock, [&]() { return job_ready; });
    printf("Received payload: %d\n", payload);
}

producer.join();
```

### 2. Timed Waits (`wait_for`)
```cpp
SqliteThreadMutexGuard lock(mutex);

// Wait up to 250 milliseconds for condition:
bool success = cond.wait_for(lock, 250, [&]() { return job_ready; });
if (!success) {
    printf("Operation timed out!\n");
}
```

### 3. Multi-Worker Broadcast (`notify_all`)
```cpp
// Wake up all waiting worker threads simultaneously
{
    SqliteThreadMutexGuard lock(mutex);
    shutdown_flag = true;
}
cond.notify_all();
```

---

## 5. Pure C API Quickstart (`sqlite3_thread.h`)

For pure C extensions (C99/C11):

```c
#include "sqlite3_thread.h"
#include <stdio.h>

static void* worker_routine(void* arg) {
    int* val = (int*)arg;
    sqlite3_time_sleep_ms(50);
    *val += 100;
    return (void*)(uintptr_t)999;
}

void run_c_threads(void) {
    sqlite3_thread_t th;
    int data = 5;

    // 1. Create thread
    sqlite3_thread_create(&th, worker_routine, &data);

    // 2. Join thread and retrieve return code
    void* retval = NULL;
    sqlite3_thread_join(&th, &retval);

    printf("Result data: %d, Retval: %llu\n", data, (unsigned long long)(uintptr_t)retval);
}
```

### Pure C Condition Variable Coordination
```c
sqlite3_thread_mutex_t mutex;
sqlite3_cond_t cond;
int ready = 0;

sqlite3_thread_mutex_init(&mutex);
sqlite3_cond_init(&cond);

// Worker waiting:
sqlite3_thread_mutex_lock(&mutex);
while (!ready) {
    sqlite3_cond_wait(&cond, &mutex);
}
sqlite3_thread_mutex_unlock(&mutex);

// Cleanup:
sqlite3_cond_destroy(&cond);
sqlite3_thread_mutex_destroy(&mutex);
```

---

## 6. Performance & Efficiency

| Metric | Measurement | System Characteristic |
| :--- | :--- | :--- |
| **Closure Invocation Overhead** | **0 virtual calls (Direct static dispatch)** | Single pointer dereference |
| **Thread Object Footprint** | **8 Bytes (`SqliteThread`)** | Stores single `sqlite3_thread_t` handle |
| **Condition Variable Footprint**| **8 Bytes (`SqliteConditionVariable`)** | Wraps OS primitive pointer/struct |
| **Memory Allocation Route** | **`sqlite3_malloc` / `sqlite3_free`** | 100% tracked in SQLite profiler |
| **Standard Library Bloat** | **0 Bytes** | Zero dependency on `libstdc++` / `libc++` |
