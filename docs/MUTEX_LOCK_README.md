# SQLite Mutex Lock (`sqlite3_mutex_lock.hpp`)

This header provides a standard C++ abstraction (`SqliteMutex` and `SqliteMutexGuard`) over SQLite's native C mutex API (`sqlite3_mutex*`). 

It is designed to give you the exact same developer experience as `std::mutex` and `std::lock_guard`, but using SQLite's internal allocators and thread-safety configurations.

## Features
- **`std::mutex` Parity**: Provides standard `lock()`, `unlock()`, and `try_lock()` methods.
- **SQLite Engine Integration**: Uses `sqlite3_mutex_alloc` to ensure the mutex primitive perfectly matches how SQLite was compiled for the target platform.
- **Single-Threaded Safety**: Natively understands SQLite's single-threaded compilation modes (which return `nullptr` for mutexes) and safely transforms locks into no-ops without crashing.
- **RAII Guards**: Ensures exception-safe unlocking via `SqliteMutexGuard`.

## Usage: `SqliteMutex`

Use `SqliteMutex` exactly as you would use `std::mutex`. It automatically manages its own lifecycle.

```cpp
#include "sqlite3_mutex_lock.hpp"

// Automatically calls sqlite3_mutex_alloc(SQLITE_MUTEX_FAST)
SqliteMutex my_mutex;

void do_work() {
    my_mutex.lock();
    // ... thread-safe operations ...
    my_mutex.unlock();
}
// Automatically calls sqlite3_mutex_free() when destroyed
```

## Usage: `SqliteMutexGuard`

Use `SqliteMutexGuard` to guarantee that your mutex is always unlocked, even if an exception is thrown or a `return` statement is triggered early in the function.

```cpp
#include "sqlite3_mutex_lock.hpp"

SqliteMutex my_mutex;

void process_data() {
    // Lock is acquired immediately upon entering scope
    SqliteMutexGuard guard(my_mutex);
    
    if (some_error) {
        return; // <-- Mutex is safely unlocked here automatically!
    }
    
    // ... normal processing ...
} // <-- Mutex is safely unlocked here automatically!
```

You can also pass raw `sqlite3_mutex*` pointers directly into the guard if you are interacting with existing SQLite C-APIs:

```cpp
void my_c_function(sqlite3_mutex* raw_mutex) {
    SqliteMutexGuard guard(raw_mutex);
    // ...
}
```
