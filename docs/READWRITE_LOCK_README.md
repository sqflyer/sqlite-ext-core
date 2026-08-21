# Read/Write Lock (Shared/Exclusive Lock)

A zero-dependency, cross-platform Read/Write lock built natively for SQLite extensions. It maximizes concurrency by allowing multiple readers simultaneously, while ensuring exclusive access for writers.

## Features
- **Zero Standard Library Bloat**: Does not require `<shared_mutex>` or `<mutex>`.
- **Cross-Platform**: Natively maps to Windows `SRWLOCK`, POSIX `pthread_rwlock_t`, and WASM `memory.atomic.wait32` (via `TinyLock`).
- **RAII Guards**: Provides C++ guards for guaranteed exception-safe locking and unlocking.

## C API (`sqlite3_rw_lock.h`)

The pure C layer is incredibly fast and lightweight. It provides macros that map directly to OS-native functions at compile-time.

```c
#include "sqlite3_rw_lock.h"

// 1. Declare the lock
sqlite3_rw_lock my_lock;

// 2. Initialize it
sqlite3_rw_lock_init(&my_lock);

// 3. Read (Shared) Access
sqlite3_rw_lock_read_acquire(&my_lock);
// ... multiple threads can do this safely ...
sqlite3_rw_lock_read_release(&my_lock);

// 4. Write (Exclusive) Access
sqlite3_rw_lock_write_acquire(&my_lock);
// ... only ONE thread can do this ...
sqlite3_rw_lock_write_release(&my_lock);

// 5. Cleanup
sqlite3_rw_lock_destroy(&my_lock);
```

## C++ API (`sqlite3_rw_lock.hpp`)

The C++ API wraps the C macros into a modern, object-oriented `SqliteRwLock` class, and provides `SqliteReadGuard` and `SqliteWriteGuard` to ensure you never accidentally leave a lock acquired.

```cpp
#include "sqlite3_rw_lock.hpp"

class SharedCache {
private:
    SqliteRwLock m_lock;
    int m_data;

public:
    SharedCache() : m_data(0) {}

    // Multiple threads can read concurrently!
    int get_data() {
        SqliteReadGuard guard(m_lock);
        return m_data;
        // Lock automatically released when guard goes out of scope!
    }

    // Only one thread can write at a time
    void update_data(int new_val) {
        SqliteWriteGuard guard(m_lock);
        m_data = new_val;
        // Lock automatically released when guard goes out of scope!
    }
};
```
