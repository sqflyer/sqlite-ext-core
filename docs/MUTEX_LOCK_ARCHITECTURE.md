# Mutex Lock Architecture

The `SqliteMutex` class provides a thin abstraction layer over the SQLite C API. Unlike `<mutex>` from the C++ standard library, this abstraction delegates the actual locking mechanics to the host SQLite engine.

## Why not use `std::mutex`?

If an extension uses `std::mutex`, the C++ standard library creates a POSIX `pthread_mutex_t` (on Linux) or a `CRITICAL_SECTION` / `SRWLOCK` (on Windows). 

This is problematic for a few reasons:
1. **No-Std Constraint**: `std::mutex` requires linking against the C++ standard library (`libstdc++`), which violates the zero-dependency requirements of embedded SQLite extensions.
2. **Compilation Mismatches**: If SQLite is compiled specifically in Single-Threaded mode (`SQLITE_THREADSAFE=0`), `std::mutex` will still force the OS to generate heavy locking primitives, resulting in useless performance overhead. 

By using `sqlite3_mutex_alloc(SQLITE_MUTEX_FAST)`, we guarantee that our C++ lock uses the exact same underlying OS primitives (and compiler optimizations) as the host database engine itself.

## Null-Pointer Safety and Single-Threaded Mode

SQLite can be compiled in three different threading modes:
1. **Serialized** (`SQLITE_THREADSAFE=1`): Fully multi-threaded.
2. **Multi-thread** (`SQLITE_THREADSAFE=2`): Multi-threaded, but individual DB connections cannot be shared across threads.
3. **Single-thread** (`SQLITE_THREADSAFE=0`): No thread safety.

When SQLite is compiled in Single-Threaded mode, the `sqlite3_mutex_alloc()` function returns a `nullptr` to completely bypass the performance overhead of locking. 

If a C++ wrapper blindly called `sqlite3_mutex_enter(nullptr)`, the program would crash with a Segmentation Fault. 

### The C++ Fix
To architecturally support all SQLite compilation modes flawlessly, `SqliteMutex` and `SqliteMutexGuard` check the pointer before every operation:

```cpp
void lock() noexcept {
    if (m_mutex) { // Safely bypasses if SQLite is in Single-Threaded mode
        sqlite3_mutex_enter(m_mutex);
    }
}
```

This ensures that the exact same C++ codebase can be compiled into a multi-threaded server backend (where the locks will function normally) and a single-threaded WebAssembly frontend (where the locks instantly become 0-cost no-ops) without changing a single line of extension code!
