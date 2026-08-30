# Async & Threading Test Suite Specification (`tests/threads`)

Comprehensive verification suite for the zero-dependency SQLite Extension Async Subsystem, covering Pure C99 and freestanding C++11/C++20 threading, condition variables, stackful coroutines/fibers, M:N cooperative schedulers, and tagged extension worker pools.

---

## 1. Architectural Overview & Test Matrix

The SQLite Async Subsystem is engineered to provide high-throughput concurrency, asynchronous Table-Valued Function (TVF) streaming, and background thread execution within SQLite extension environments without depending on the C++ Standard Library (`-nostdlib++` compliant) or external runtimes.

The test suite enforces a dual-implementation model:
1. **Pure C99 Interfaces**: Direct native OS wrappers (`CRITICAL_SECTION`, `CONDITION_VARIABLE`, `CreateFiberEx` / `ucontext_t`).
2. **C++11/C++20 RAII Abstractions**: Zero-overhead type-erased closure holders, move-only resource management, and range-based generator iterators.

### Test Binary Matrix (66 Total Test Cases)

| Binary | Source File | Standard | Covered Subsystem | Test Cases |
| :--- | :--- | :--- | :--- | :---: |
| [`test_thread_c`](file:///c:/msys64/home/dilipvamsi/works/repos/sqlite-ext-core/tests/threads/test_thread_c.c) | `test_thread_c.c` | C99 | OS Thread Lifecycle & Joining | 3 |
| [`test_thread`](file:///c:/msys64/home/dilipvamsi/works/repos/sqlite-ext-core/tests/threads/test_thread.cpp) | `test_thread.cpp` | C++11 | `SqliteThread` (Lambdas, Move, Detach) | 4 |
| [`test_cond_c`](file:///c:/msys64/home/dilipvamsi/works/repos/sqlite-ext-core/tests/threads/test_cond_c.c) | `test_cond_c.c` | C99 | OS Mutex & Condition Variables | 3 |
| [`test_cond`](file:///c:/msys64/home/dilipvamsi/works/repos/sqlite-ext-core/tests/threads/test_cond.cpp) | `test_cond.cpp` | C++11 | `SqliteConditionVariable` & Predicates | 3 |
| [`test_coro_c`](file:///c:/msys64/home/dilipvamsi/works/repos/sqlite-ext-core/tests/threads/test_coro_c.c) | `test_coro_c.c` | C99 | Stackful Fibers (`sqlite3_coro.h`) | 9 |
| [`test_coro`](file:///c:/msys64/home/dilipvamsi/works/repos/sqlite-ext-core/tests/threads/test_coro.cpp) | `test_coro.cpp` | C++11/20 | `SqliteCoroutine` & `SqliteFiberGenerator<T>` | 10 |
| [`test_coro_sched_c`](file:///c:/msys64/home/dilipvamsi/works/repos/sqlite-ext-core/tests/threads/test_coro_sched_c.c) | `test_coro_sched_c.c` | C99 | M:N Scheduler (`sqlite3_coro_sched.h`) | 11 |
| [`test_coro_sched`](file:///c:/msys64/home/dilipvamsi/works/repos/sqlite-ext-core/tests/threads/test_coro_sched.cpp) | `test_coro_sched.cpp` | C++11/20 | `SqliteCoroScheduler` & Global Pool | 13 |
| [`test_coro_ext_pool_c`](file:///c:/msys64/home/dilipvamsi/works/repos/sqlite-ext-core/tests/threads/test_coro_ext_pool_c.c) | `test_coro_ext_pool_c.c` | C99 | Tagged Extension Pool Registry | 5 |
| [`test_coro_ext_pool`](file:///c:/msys64/home/dilipvamsi/works/repos/sqlite-ext-core/tests/threads/test_coro_ext_pool.cpp) | `test_coro_ext_pool.cpp` | C++11/20 | `SqliteExtCoroPool<Tag>` & Template Isolation | 5 |
| **Total** | **10 binaries** | | | **66 test cases** |

---

## 2. Detailed Test Specifications by Subsystem

### A. OS Threads Subsystem

#### 1. Pure C Thread Lifecycle (`test_thread_c.c`)
- **`test_thread_create_and_join()`**:
  - *Objective*: Verifies OS thread allocation, parameter passing, barrier join synchronization, and return value propagation.
  - *Mechanics*: Spawns a background thread passing an integer payload (`42`), blocks on `sqlite3_thread_join()`, and asserts that the worker modified the payload to `142` and returned pointer `(void*)1234`.
  - *Invariant*: `sqlite3_thread_join` releases underlying OS thread handles and captures `retval` safely.
- **`test_thread_detach()`**:
  - *Objective*: Verifies asynchronous detachment without parent thread blocking.
  - *Mechanics*: Creates a thread with `sqlite3_thread_create`, immediately detaches it via `sqlite3_thread_detach()`, and verifies that the thread continues executing independently and frees its internal `sqlite3_win32_thctx_t` heap context upon termination.
- **`test_thread_yield()`**:
  - *Objective*: Verifies cooperative CPU timeslice relinquishment.
  - *Mechanics*: Calls `sqlite3_thread_yield()` (mapping to `SwitchToThread()` on Windows and `sched_yield()` on POSIX) to ensure zero crashes or stalls.

#### 2. C++11 RAII `SqliteThread` (`test_thread.cpp`)
- **`test_thread_function_ptr()`**:
  - *Objective*: Tests spawning and joining threads using parameterless C++ function pointers (`void (*)()`).
- **`test_thread_lambda_capture()`**:
  - *Objective*: Tests type-erased closure execution with capturing state without `<functional>`.
  - *Mechanics*: Instantiates `SqliteThread` with a lambda mutating captured stack state `val = 999`. Memory is allocated via `CallableHolder<F>` using SQLite's memory allocator (`sqlite_new` / `sqlite_delete`).
- **`test_thread_move_semantics()`**:
  - *Objective*: Validates move constructor and move assignment operator semantics.
  - *Mechanics*: Constructs `th1`, moves into `th2` via move constructor, then moves `th2` into `th3` via move assignment. Verifies `joinable()` state transfers accurately (`th1` and `th2` become non-joinable; `th3` joins successfully).
- **`test_thread_detach()`**:
  - *Objective*: Validates explicit `th.detach()` and automatic destructor cleanup.

---

### B. Condition Variables & Mutexes

#### 1. Pure C Synchronization (`test_cond_c.c`)
- **`test_cond_signal()`**:
  - *Objective*: Verifies single-worker unblocking via `sqlite3_cond_signal()`.
  - *Mechanics*: Worker locks `sqlite3_thread_mutex_t`, sets `ready = 1` and `counter = 777`, signals condition variable, and unlocks. Main thread blocks on `sqlite3_cond_wait()` in a `while (!ctx.ready)` loop.
- **`test_cond_timedwait()`**:
  - *Objective*: Verifies high-precision millisecond timeout detection.
  - *Mechanics*: Calls `sqlite3_cond_timedwait(&cond, &mutex, 30)` on an unsignaled condition variable. Asserts return code indicates timeout and elapsed time is $\ge 20\text{ ms}$.
- **`test_cond_broadcast()`**:
  - *Objective*: Verifies multi-worker broadcast unblocking via `sqlite3_cond_broadcast()`.
  - *Mechanics*: Spawns 4 concurrent worker threads blocking on `sqlite3_cond_wait()`. Main thread signals `sqlite3_cond_broadcast()`. Asserts all 4 workers wake up and increment shared counter to 4.

#### 2. C++11 `SqliteConditionVariable` (`test_cond.cpp`)
- **`test_condition_variable_predicate()`**:
  - *Objective*: Verifies loop-guarded predicate waiting (`cv.wait(guard, [&]() { return ready; })`).
  - *Invariant*: Automatically protects against spurious wakeups without manual while loops.
- **`test_condition_variable_wait_for()`**:
  - *Objective*: Verifies timed predicate waiting (`cv.wait_for(guard, timeout_ms, predicate)`).
- **`test_condition_variable_broadcast()`**:
  - *Objective*: Tests waking multiple C++ worker threads simultaneously via `cv.notify_all()`.

---

### C. Stackful Coroutines & Fibers

#### 1. Pure C Coroutine Primitives (`test_coro_c.c`)
- **`test_coro_basic_lifecycle()`**:
  - *Objective*: Validates fiber initialization, step-by-step cooperative yields, and completed state detection.
  - *Mechanics*: Coroutine yields at values `100`, `200`, and `300`. Verifies `sqlite3_coro_is_done()` is false during yields and true after final return. Asserts subsequent `resume()` returns `SQLITE_MISUSE`.
- **`test_coro_deep_stack_yielding()`**:
  - *Objective*: Verifies yielding from deeply nested helper functions (stack unwinding and preservation).
- **`test_coro_interleaved()`**:
  - *Objective*: Round-robin interleaved execution between two distinct coroutines (`coro1` and `coro2`).
- **`test_coro_early_cancellation()`**:
  - *Objective*: Destroying suspended coroutines before completion via `sqlite3_coro_destroy()` without resource leaks.
- **`test_coro_main_thread_yield_and_double_destroy()`**:
  - *Objective*: Verifies that invoking `sqlite3_coro_yield()` from the main OS thread (outside any coroutine) is safely handled as a no-op without crashes. Also tests double `sqlite3_coro_destroy()` safety.
- **`test_coro_struct_channeling()`**:
  - *Objective*: Channeling heap and stack pointers across fiber boundaries using `sqlite3_coro_yield_value()` and `sqlite3_coro_get_value()`.
- **`test_coro_deep_stack_variables()`**:
  - *Objective*: Validates stack frame integrity across yields under deep recursion (large stack frames).
- **`test_coro_many_concurrent()`**:
  - *Objective*: Concurrently instantiates and schedules 20 independent coroutines in round-robin sequence (80 total context switches).
- **`test_coro_error_handling()`**:
  - *Objective*: Verifies NULL parameter robustness across all C coroutine APIs (`sqlite3_coro_create`, `sqlite3_coro_resume`, `sqlite3_coro_get_value`, `sqlite3_coro_is_done`, `sqlite3_coro_destroy`).

#### 2. C++11/C++20 `SqliteCoroutine` & `SqliteFiberGenerator<T>` (`test_coro.cpp`)
- **`test_coro_lambda_captures()`**:
  - *Objective*: Capturing lambda closures mutating local state across multiple yields.
- **`test_coro_move_semantics()`**:
  - *Objective*: Transfers coroutine ownership across instances in 1 CPU cycle using move constructors and move assignment.
- **`test_coro_typed_channeling()`**:
  - *Objective*: Zero-allocation pointer value channeling across yields.
- **`test_coro_generator_range_for()`**:
  - *Objective*: Stackful Table-Valued Function (TVF) generator iterating via standard C++11 range-based for loop:
    ```cpp
    SqliteFiberGenerator<int> gen([](const auto& yield) {
        for (int i = 1; i <= 5; ++i) yield(i * 10);
    });
    for (int val : gen) { /* 10, 20, 30, 40, 50 */ }
    ```
- **`test_coro_generator_move_and_string_view()`**:
  - *Objective*: Moving `SqliteFiberGenerator` instances yielding zero-copy `SqliteStringView` slices.
- **`test_coro_edge_cases()`**:
  - *Objective*: Default construction, self-move assignment, and reassignment over active generators.
- **`test_coro_early_break_and_composition()`**:
  - *Objective*: Early break from range-for loop triggering RAII fiber cleanup, empty generator iteration, and composing one generator within another.
- **`test_coro_polymorphic_value_streaming()`**:
  - *Objective*: Streaming polymorphic `SqliteValueOwned` objects (integers, doubles, strings, blobs) across fiber yields with strict type inspection.
- **`test_coro_multi_column_row_streaming()`**:
  - *Objective*: Streaming multi-column fixed-schema database rows (`SqliteValueTuple<4>`) across yields with 0 heap overhead.
- **`test_coro_dynamic_array_reallocation()`**:
  - *Objective*: Reallocating dynamic arrays (`sqlite_reallocate_array`) inside fiber routines across yield boundaries.

---

### D. M:N Coroutine Scheduler Subsystem

#### 1. Pure C Scheduler (`test_coro_sched_c.c`)
- **`test_main_thread_scheduler()`**:
  - *Objective*: Main-thread synchronous event loop (`num_workers = 0`). Tasks are stepped via `sqlite3_coro_pool_poll_one()` and drained via `sqlite3_coro_pool_run_until_empty()`.
- **`test_multithreaded_scheduler()`**:
  - *Objective*: Dispatches 50 tasks across 4 background OS worker threads. Asserts total accumulated sum equals `5550`.
- **`test_interleaved_yielding()`**:
  - *Objective*: Interleaved cooperative task execution where tasks yield control back to the ready queue and resume across available worker threads.
- **`test_high_concurrency_batch()`**:
  - *Objective*: High-concurrency fan-out of 100 batch tasks across 8 worker threads.
- **`test_scheduler_shutdown_with_pending()`**:
  - *Objective*: Graceful teardown when tasks remain suspended or pending in the ready queue. Verifies that `sqlite3_coro_pool_destroy()` cleanly frees all remaining fiber handles.
- **`test_null_and_error_handling()`**:
  - *Objective*: Parameter safety and rejection of NULL pointers.
- **`test_run_until_empty()`**:
  - *Objective*: Synchronously drains multi-step yielding tasks on the main thread until the task queue is completely empty.
- **`test_nested_task_spawning()`**:
  - *Objective*: Spawning child coroutine tasks dynamically from inside a running worker fiber.
- **`test_custom_stack_sizes()`**:
  - *Objective*: Allocating custom fiber stack sizes (32 KB and 128 KB) and preserving stack frame integrity across yields.
- **`test_multi_phase_pool_reuse()`**:
  - *Objective*: Reusing a single `sqlite3_coro_pool_t` instance across 3 sequential execution phases with `sqlite3_coro_pool_wait()`.
- **`test_out_of_coroutine_yield_safety()`**:
  - *Objective*: Calling `sqlite3_coro_pool_yield()` from a non-coroutine thread safely returns without corrupting thread state.

#### 2. C++11/C++20 `SqliteCoroScheduler` (`test_coro_sched.cpp`)
- **`test_cpp_main_thread_scheduler()`**:
  - *Objective*: Single-threaded event loop testing using `scheduler.poll_one()` and `scheduler.run_local()`.
- **`test_cpp_multithreaded_scheduler()`**:
  - *Objective*: 50 capturing lambda closures dispatched across 4 worker threads with `scheduler.wait_all()`.
- **`test_cpp_global_scheduler_refcounting()`**:
  - *Objective*: Process-wide global scheduler singleton (`SqliteCoroScheduler::acquire_global(4)` and `release_global()`). Verifies atomic reference counting across multiple database handles.
- **`test_cpp_move_semantics_and_raii()`**:
  - *Objective*: Move constructor and assignment for `SqliteCoroScheduler` with safe RAII teardown.
- **`test_cpp_multi_stage_pipeline()`**:
  - *Objective*: 20 multi-stage pipeline closures yielding intermediate results across worker threads.
- **`test_cpp_standalone_spawn_helpers()`**:
  - *Objective*: Standalone `sqlite_coro_spawn()` template helpers supporting scheduler references, scheduler pointers, and raw C `sqlite3_coro_pool_t*` handles.
- **`test_cpp_custom_stack_sizes()`**:
  - *Objective*: Template-based compile-time stack sizing via `sqlite_coro_spawn_stack<Size>()`.
- **`test_cpp_high_concurrency_batch()`**:
  - *Objective*: High-throughput stress test (50 tasks executing 100 total operations across 8 workers).
- **`test_cpp_run_until_empty()`**:
  - *Objective*: Synchronous batch draining on main thread (`scheduler.run_until_empty()`).
- **`test_cpp_custom_functor_and_mutable_lambdas()`**:
  - *Objective*: Executing custom callable functor objects and stateful mutable lambdas.
- **`test_cpp_multi_stage_sequential_pipeline()`**:
  - *Objective*: 3-stage sequential data pipeline processing 225 items through reusable scheduler barriers.
- **`test_cpp_scheduler_validity_and_getters()`**:
  - *Objective*: Inspecting `is_valid()`, `worker_count()`, `pending_tasks()`, and `raw_pool()`.
- **`test_cpp_out_of_coroutine_yield_safety()`**:
  - *Objective*: Yield safety from main non-fiber thread.

---

### E. Tagged Extension Worker Pools

#### 1. Pure C Tagged Pools (`test_coro_ext_pool_c.c`)
- **`test_ext_pool_acquisition_and_refcounting()`**:
  - *Objective*: Zero-collision static address token keying (`SQLITE_EXT_TAG_DECLARE(Tag)` and `SQLITE_EXT_TAG(Tag)`). Verifies atomic reference counting across multiple simulated database opens/closes.
- **`test_multiple_ext_pools_isolation()`**:
  - *Objective*: Spawns tasks across distinct extension pools (`VectorExtTag` and `CryptoExtTag`) and verifies complete pool isolation and independent worker execution.
- **`test_ext_pool_wait_barrier()`**:
  - *Objective*: Draining extension queues via `sqlite3_coro_ext_pool_wait()`.
- **`test_ext_pool_shutdown_all()`**:
  - *Objective*: Forcible process-wide teardown of all registered extension pools via `sqlite3_coro_ext_pool_shutdown_all()`.
- **`test_ext_pool_null_safety()`**:
  - *Objective*: Passing NULL tag falls back safely to default pool.

#### 2. C++11/C++20 `SqliteExtCoroPool<Tag>` (`test_coro_ext_pool.cpp`)
- **`test_cpp_tagged_pool_isolation()`**:
  - *Objective*: Type-safe template isolation between distinct tag types (`SqliteExtCoroPool<VectorExtensionTag>` vs `SqliteExtCoroPool<SearchExtensionTag>`).
- **`test_cpp_auto_acquire_spawn()`**:
  - *Objective*: Submitting capturing closures via `sqlite_coro_ext_spawn<Tag>()` with automatic on-demand pool acquisition.
- **`test_cpp_default_pool_alias()`**:
  - *Objective*: Verifies default template tag pool `SqliteExtCoroPool<>` and its alias `SqliteExtensionCoroPool`.
- **`test_cpp_tagged_wrapper_class()`**:
  - *Objective*: Tests static utility wrapper class `SqliteTaggedCoroPool`.
- **`test_cpp_forcible_shutdown_and_edge_cases()`**:
  - *Objective*: Forcible shutdown (`SqliteExtCoroPool<Tag>::shutdown()`) and inactive pool status inspection.

---

## 3. Compilation & Execution Guide

### Windows MSVC (`cl.exe`)
```cmd
cd tests\threads
make.bat clean
make.bat build
make.bat test
```

### MSYS2 / GCC / Clang
```bash
cd /home/dilipvamsi/works/repos/sqlite-ext-core/tests/threads
make clean
make all
make test
```
