# SQLite Extension State Manager (C/C++)

This header (`sqlite3_ext_state.h`) provides a zero-dependency, zero-boilerplate macro that automatically generates a thread-safe, garbage-collected, **Per-Database Shared State Registry** for your SQLite extensions.

## Why do I need this?
If you want your SQLite extension to maintain state (like a connection pool, an LRU cache, or a simple counter), you cannot use global variables because that state would be illegally shared across every database loaded in the same process. You must instantiate state exactly once per database file. This macro does all of that heavy lifting for you automatically.

## Quickstart

### 1. Define your State
```c
#include "sqlite3ext.h"
#include "sqlite3_ext_state.h"

// Define a struct holding whatever state you need
typedef struct {
    int counter;
} SharedState;

// Generate the registry!
SQLITE_EXTENSION_STATE(SharedState)
```

### 2. Initialize in your Extension Hook
Inside your `sqlite3_extension_init` function, initialize the state and pass the raw pointer and destructor to your scalar functions:

```c
// Optional: Custom initialization (runs exactly once per database file)
static void init_shared_state(SharedState* state) {
    state->counter = 100;
}

int sqlite3_myext_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi) {
    SQLITE_EXTENSION_INIT2(pApi);

    // Initialize the state (Arg 2 is your init function, Arg 3 is an optional free function)
    void* raw_state = SharedState_init(db, init_shared_state, NULL);
    
    return sqlite3_create_function_v2(
        db, "test_counter", 0, SQLITE_UTF8, 
        raw_state,             // Pass the raw state pointer
        test_counter_func, 
        NULL, NULL, 
        SharedState_destructor // Pass the generated destructor!
    );
}
```

### 3. Use it in your Functions (C++)
If you compile with a C++ compiler (`g++`, `clang++`, MSVC), the macro automatically generates RAII lock guards for perfect exception-safety.

```cpp
static void test_counter_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    auto state = SharedState_from_context(ctx);
    if (!state) return;
    
    // Automatically acquires the Write Lock, and releases it at the end of the curly braces!
    {
        SharedState_WriteGuard lock(state);
        lock->counter++;
    } 
    
    // Automatically acquires the Read Lock!
    int val = 0;
    {
        SharedState_ReadGuard lock(state);
        val = lock->counter;
    }
    
    sqlite3_result_int64(ctx, (sqlite3_int64)val);
}
```

### 3 (Alternative). Use it in your Functions (Pure C)
If you are compiling in pure C, you must manually acquire and release the read/write locks.

```c
static void test_counter_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    SharedState *state = SharedState_from_context(ctx);
    if (!state) return;
    
    // Write Lock
    SharedState_write_acquire(state);
    state->counter++;
    SharedState_write_release(state);
    
    sqlite3_result_int64(ctx, (sqlite3_int64)state->counter);
}
```

## Features
- **Cross-Platform**: Uses native Read/Write locks on Windows (`SRWLOCK`), macOS/Linux (`pthread_rwlock_t`), and WebAssembly (`sqlite3_mutex`).
- **Zero-Overhead Hot Path**: Caches state lookups in SQLite's O(1) auxdata.
- **Memory Safe**: Automatically manages memory via SQLite's `xDestroy` hooks. No leaks.

For a detailed breakdown of the internal architecture, see `EXT_STATE_ARCHITECTURE.md`.
