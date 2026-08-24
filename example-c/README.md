# Pure C SQLite Extension Example

This directory contains a complete, working example of creating, compiling, and loading a native SQLite extension written in **Pure C (C99/C11)** using `sqlite-ext-core`.

---

## 1. Directory Contents

- **[`example.c`](example.c)**: Pure C implementation of a loadable extension demonstrating:
  - **Shared State Management**: Initializing `CAnalyticsState` across queries via `sqlite3_ext_state.h`.
  - **Stateless Scalar Function**: `c_math_hypot(a, b)`.
  - **Stateful Scalar Function**: `c_analytics_ping()`.
  - **Custom Aggregate Function**: `c_sum_squares(val)` with `xStep` and `xFinal`.
  - **C Extension Macros**: `SQLITE_C_EXTENSION_ENTRYPOINT(c_example, db)` and `SQLITE_C_DEFAULT_EXTENSION_ENTRYPOINT(db)`.
- **[`example.sql`](example.sql)**: SQL script loading `./build/libc_example` and executing test queries.
- **[`Makefile`](Makefile)**: Compiles `example.c` with `gcc` into `build/libc_example.dll` (or `.so` / `.dylib`) and runs `example.sql`.

---

## 2. Quickstart

### Build and Run Demo with SQLite CLI
```bash
make run
```

### Expected Output
```
┌──────────────────────────────────────────────────────────────────┐
│                           test_banner                            │
├──────────────────────────────────────────────────────────────────┤
│ === 1. Pure C Stateless Scalar Function: c_math_hypot(3, 4) ===  │
└──────────────────────────────────────────────────────────────────┘
┌─────┬─────┬────────────┬─────────────────┐
│  a  │  b  │ hypotenuse │ pythagorean_10  │
├─────┼─────┼────────────┼─────────────────┤
│ 3.0 │ 4.0 │ 5.0        │ 10.0            │
└─────┴─────┴────────────┴─────────────────┘
┌──────────────────────────────────────────────────────────────────┐
│                           test_banner                            │
├──────────────────────────────────────────────────────────────────┤
│ === 2. Pure C Stateful Scalar Function: c_analytics_ping() ===   │
└──────────────────────────────────────────────────────────────────┘
┌───────────────┐
│ query_count_1 │
├───────────────┤
│ 1             │
└───────────────┘
┌───────────────┐
│ query_count_2 │
├───────────────┤
│ 2             │
└───────────────┘
┌───────────────┐
│ query_count_3 │
├───────────────┤
│ 3             │
└───────────────┘
┌──────────────────────────────────────────────────────────────────┐
│                           test_banner                            │
├──────────────────────────────────────────────────────────────────┤
│ === 3. Pure C Custom Aggregate Function: c_sum_squares(val) ===  │
└──────────────────────────────────────────────────────────────────┘
┌───────────────┬────────────────┐
│    inputs     │ sum_of_squares │
├───────────────┼────────────────┤
│ 1, 2, 3, 4, 5 │ 55             │
└───────────────┴────────────────┘
```

---

## 3. Manual Compilation

### Linux / macOS (GCC / Clang)
```bash
mkdir -p build
gcc -shared -fPIC -O2 -std=c99 -Wall -Wextra \
    -I../include -o build/libc_example.so example.c
```

### Windows (MSYS2 / MinGW GCC)
```bash
mkdir -p build
gcc -shared -fPIC -O2 -std=c99 -Wall -Wextra \
    -I../include -o build/libc_example.dll example.c
```

---

## 4. Shared State & Pluggable Lock Policies (Pure C)

The example uses `sqlite3_ext_state.h` to maintain per-database shared state with zero global variable contamination. You can choose the synchronization primitive that best fits your workload:

| Lock Policy | Macro Declaration | Optimal Workload |
| :--- | :--- | :--- |
| **Read/Write Lock** (Default) | `SQLITE_EXTENSION_STATE_DECLARE(State)` / `_DECLARE_RW` | Read-heavy state, lookup tables, caches |
| **Tiny Lock** (1-Byte Spinlock) | `SQLITE_EXTENSION_STATE_DECLARE_TINY(State)` | In-memory key-value stores (`memkv`), counters, metrics |
| **SQLite Mutex** | `SQLITE_EXTENSION_STATE_DECLARE_MUTEX(State)` | Native engine mutex integration & profiling |
| **Custom Lock Adapter** | `SQLITE_EXTENSION_STATE_DECLARE_WITH_LOCK(State, LockType)` | Custom locking primitives |

