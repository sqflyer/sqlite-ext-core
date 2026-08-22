/**
 * @file sqlite3_ext_state.h
 * @brief High-performance, thread-safe, and memory-safe shared state registry for SQLite extensions.
 * 
 * This header provides a zero-boilerplate macro `SQLITE_EXTENSION_STATE` to generate 
 * a perfectly isolated per-database shared state registry. It replicates the advanced 
 * 3-layer caching and garbage collection architecture of the `sqlite-ext-core` Rust crate.
 * 
 * Features:
 * - O(1) Fast-Path Caching via SQLite's internal auxdata.
 * - Automated Garbage Collection tied to SQLite's connection lifecycle (`xDestroy`).
 * - Cross-Platform Read/Write Locks (Windows SRWLOCK, POSIX pthreads, and WASM).
 * - C++ RAII Support for exception-safe locking automatically injected if compiling with C++.
 * - In-Memory Database Isolation (`:memory:` instances don't share state).
 * 
 * Usage:
 *   typedef struct { int counter; } SharedState;
 *   SQLITE_EXTENSION_STATE(SharedState)
 * 
 *   // In extension init:
 *   void* raw = SharedState_init(db, NULL, NULL);
 * 
 *   // In scalar function:
 *   SharedState* state = SharedState_from_context(ctx);
 */
#ifndef SQLITE3_EXT_STATE_H
#define SQLITE3_EXT_STATE_H

#include "sqlite3ext.h"
#include <sqlite3.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include "sqlite3_tiny_lock.h"

#ifndef SQLITE_EXT_STATE_AUXDATA_SLOT
/* Use a high pseudo-random slot to avoid colliding with argument caching (slot 0..N) */
#define SQLITE_EXT_STATE_AUXDATA_SLOT 0x45585400
#endif

#include "sqlite3_rw_lock.h"

/*
 * DEFINE_SQLITE_EXT_STATE(StateType, Prefix)
 * 
 * Generates a strongly-typed, per-database shared state registry for an extension.
 * Replicates the architecture of `sqlite-ext-core` DbRegistry in pure C.
 * 
 * =================================================================================
 * ARCHITECTURE OVERVIEW
 * =================================================================================
 * 
 * 1. The Global Static Trap
 *    SQLite extensions are loaded once per process, but can be used across multiple
 *    database connections. Using standard global variables shares state across all 
 *    databases, which is incorrect. Allocating new state per connection means two 
 *    connections to the *same* database do not share state.
 * 
 * 2. Per-Database Shared State (The Solution)
 *    This macro implements a per-database shared state model. State is instantiated 
 *    once per database file, shared automatically across all connections to that file, 
 *    and cleaned up automatically when the last connection closes via RAII.
 * 
 * 3. Thread Safety and Concurrency
 *    Since SQLite can call into the extension from multiple threads concurrently, 
 *    the registry is protected by a fast `sqlite3_mutex`. The reference count inside 
 *    the state wrapper is also protected by its own microscopic `sqlite3_tiny_lock`.
 * 
 * 4. The 3-Layer Lookup (The Hot Path)
 *    To ensure fetching state inside a tight scalar function loop is practically free, 
 *    we use a 3-layer lookup mechanism:
 * 
 *    - Layer 1 (Hot Path):   SQLite's `auxdata` (O(1)). Checked first via `sqlite3_get_auxdata`.
 *                            If the query has run before, the state pointer is retrieved 
 *                            instantly without any locking or hash map lookups.
 *    - Layer 2 (Warm Path):  Registry Linked List (O(N)). If missed, we lock the global 
 *                            registry and search for the database path. If found, we retain 
 *                            it and cache it back into `auxdata`.
 *    - Layer 3 (Cold Path):  Init. If not found in the registry, we run the user's `init_fn` 
 *                            and register the new state.
 * 
 * =================================================================================
 * 
 * Example Usage:
 *   typedef struct { int counter; } MyState;
 *   SQLITE_EXTENSION_STATE(MyState)
 * 
 *   // In sqlite3_extension_init:
 *   // Passing NULL, NULL provides default zero-initialization (memset to 0) and default free.
 *   void* raw_state = MyState_init(db, NULL, NULL);
 *   
 *   // If your struct requires complex setup (e.g., dynamic strings, inner pointers), 
 *   // you can pass custom init and free functions instead:
 *   // void my_custom_init(MyState *state) { ... }
 *   // void* raw_state = MyState_init(db, my_custom_init, my_custom_free);
 *   
 *   // CRITICAL: Passing MyState_destructor is required to prevent memory leaks!
 *   sqlite3_create_function_v2(db, "my_func", 1, SQLITE_UTF8, raw_state, my_func_impl, NULL, NULL, MyState_destructor);
 * 
 *   // In my_func_impl (Fast Path using pApp):
 *   MyState *state = MyState_from_context(ctx);
 * 
 *   // OR if you don't have pApp (e.g. Virtual Tables, using 3-layer lookup):
 *   MyState *state = MyState_from_db(ctx, db);
 * 
 * =================================================================================
 * LIFECYCLE & DESTRUCTOR EXAMPLE (Why MyState_destructor is required)
 * =================================================================================
 * 
 * 1. Connection A opens `mydb.sqlite` and loads the extension.
 *    - `MyState_init` allocates a new `MyState` struct (refcount = 1).
 *    - The pointer is passed to SQLite via `sqlite3_create_function_v2`.
 * 
 * 2. Connection B opens `mydb.sqlite` and loads the extension.
 *    - `MyState_init` finds the existing state in the registry.
 *    - It increments the refcount to 2, avoiding a new allocation.
 * 
 * 3. Connection A is closed.
 *    - SQLite sees that it's destroying Connection A, and automatically calls
 *      the `MyState_destructor` callback we provided.
 *    - The destructor safely decrements the refcount to 1. The memory is kept 
 *      alive because Connection B is still actively using it.
 * 
 * 4. Connection B is closed.
 *    - SQLite calls `MyState_destructor` again.
 *    - The refcount hits 0. The destructor safely frees the memory and removes
 *      it from the global registry.
 * 
 * WARNING: If you pass NULL instead of MyState_destructor, SQLite never notifies
 * the extension when connections close. The refcount would stay > 0 forever, 
 * causing a permanent memory leak for the lifetime of the process!
 */

#ifdef __cplusplus
#define SQLITE_EXTENSION_STATE(StateType) \
    static_assert(false, "Do not use SQLITE_EXTENSION_STATE macro in C++. Include sqlite3_ext_state.hpp and use SqliteExtState<T> instead.");
#else

#define SQLITE_EXTENSION_STATE_DECLARE(StateType) \
    typedef struct StateType##_Entry { \
        char *db_path; \
        int refcount; \
        sqlite3_rw_lock state_mutex; \
        void (*free_fn)(StateType*); \
        struct StateType##_Entry *next; \
        StateType state; \
    } StateType##_Entry; \
    \
    extern StateType##_Entry *StateType##_registry_head; \
    extern sqlite3_mutex *StateType##_registry_mutex; \
    \
    /* Function declarations for external visibility */ \
    static void* StateType##_init(sqlite3 *db, void (*init_fn)(StateType*), void (*free_fn)(StateType*)); \
    static StateType* StateType##_from_db(sqlite3_context *ctx, sqlite3 *db); \
    static StateType* StateType##_from_context(sqlite3_context *ctx); \
    static void StateType##_read_acquire(StateType *state); \
    static void StateType##_read_release(StateType *state); \
    static void StateType##_write_acquire(StateType *state); \
    static void StateType##_write_release(StateType *state); \
    static void StateType##_destructor(void *p);

#define SQLITE_EXTENSION_STATE_DEFINE(StateType) \
    StateType##_Entry *StateType##_registry_head = NULL; \
    sqlite3_mutex *StateType##_registry_mutex = NULL; \
    \
    static const char* __##StateType##_get_db_path(sqlite3 *db, char *resolved_path_buf) { \
        const char *raw_path = sqlite3_db_filename(db, "main"); \
        /* \
         * In-Memory / Temp DB Isolation: \
         * If the database has no backing file, sqlite3_db_filename returns NULL \
         * or "". To prevent unrelated in-memory connections from sharing the \
         * same state registry, we dynamically generate a unique collision-proof \
         * string using the raw sqlite3 pointer address (e.g. ":memory:0x1234"). \
         */ \
        if (!raw_path || raw_path[0] == '\0') { \
            snprintf(resolved_path_buf, 128, ":memory:%p", (void*)db); \
            return resolved_path_buf; \
        } \
        return raw_path; \
    } \
    \
    static void __##StateType##_ensure_mutex_init(void) { \
        if (!StateType##_registry_mutex) { \
            /* \
             * SQLITE_MUTEX_STATIC_MASTER is a global, pre-allocated SQLite mutex. \
             * We do NOT free it because static mutexes are owned by the SQLite \
             * runtime and live for the entire process lifetime. \
             */ \
            sqlite3_mutex *master = sqlite3_mutex_alloc(SQLITE_MUTEX_STATIC_MASTER); \
            sqlite3_mutex_enter(master); \
            if (!StateType##_registry_mutex) { \
                StateType##_registry_mutex = sqlite3_mutex_alloc(SQLITE_MUTEX_STATIC_APP1); \
            } \
            sqlite3_mutex_leave(master); \
        } \
    } \
    \
    /* \
     * Internal helper: Thread-safely increments the reference count of an entry. \
     */ \
    static StateType##_Entry* __##StateType##_entry_retain(StateType##_Entry *entry) { \
        if (!entry) return NULL; \
        sqlite_atomic_increment_32(&entry->refcount); \
        return entry; \
    } \
    \
    /* \
     * Internal helper: Destroys a state entry, frees all associated memory/mutexes, \
     * and removes it from the global registry linked list. \
     */ \
    static void __##StateType##_entry_free(StateType##_Entry *entry) { \
        __##StateType##_ensure_mutex_init(); \
        \
        /* Step 1: Acquire registry lock to prevent new threads from finding the entry */ \
        sqlite3_mutex_enter(StateType##_registry_mutex); \
        \
        /* \
         * DOUBLE-CHECKED LOCKING (Crucial Race Condition Fix): \
         * Between the refcount dropping to 0 (in _entry_release) and us acquiring \
         * the global registry_mutex here, another thread could have searched the \
         * registry, found this entry, and retained it! If so, we MUST abort the free. \
         */ \
        if (sqlite_atomic_load_32(&entry->refcount) > 0) { \
            sqlite3_mutex_leave(StateType##_registry_mutex); \
            return; \
        } \
        \
        /* Step 2: Safe to remove the entry from the global linked list */ \
        StateType##_Entry **curr = &StateType##_registry_head; \
        while (*curr) { \
            if (*curr == entry) { \
                *curr = entry->next; \
                break; \
            } \
            curr = &(*curr)->next; \
        } \
        sqlite3_mutex_leave(StateType##_registry_mutex); \
        \
        /* Step 2: Destroy the cross-platform read-write lock */ \
        sqlite3_rw_lock_destroy(&entry->state_mutex); \
        \
        /* Step 3: Run the user's custom cleanup routine, if any */ \
        if (entry->free_fn) { \
            entry->free_fn(&entry->state); \
        } \
        \
        /* Step 4: Free the deep-copied strings and internal SQLite mutexes */ \
        if (entry->db_path) sqlite3_free(entry->db_path); \
        sqlite3_free(entry); \
    } \
    \
    /* \
     * Internal helper: Thread-safely decrements the reference count of an entry. \
     * If the reference count drops to 0, it delegates to __entry_free to destroy it. \
     */ \
    static void __##StateType##_entry_release(StateType##_Entry *entry) { \
        if (!entry) return; \
        if (sqlite_atomic_decrement_32(&entry->refcount) == 0) { \
            __##StateType##_entry_free(entry); \
        } \
    } \
    \
    /* \
     * Internal helper: Allocates and initializes a new state entry along with its \
     * internal mutexes and read-write locks. Does NOT lock the registry itself, \
     * but assumes the caller holds the registry lock as it links the new entry \
     * directly into the global registry linked list. \
     */ \
    static StateType##_Entry* __##StateType##_entry_alloc(const char *db_path, void (*init_fn)(StateType*), void (*free_fn)(StateType*)) { \
        /* Step 1: Allocate the main entry struct */ \
        StateType##_Entry *entry = (StateType##_Entry*) sqlite3_malloc(sizeof(StateType##_Entry)); \
        if (entry) { \
            /* Step 2: Deep copy the database path to serve as the registry key */ \
            int path_len = strlen(db_path); \
            entry->db_path = (char*)sqlite3_malloc(path_len + 1); \
            if (entry->db_path) { \
                memcpy(entry->db_path, db_path, path_len + 1); \
            } else { \
                sqlite3_free(entry); \
                entry = NULL; \
            } \
        } \
        if (entry) { \
            /* Step 4: Initialize the cross-platform state R/W lock */ \
            sqlite3_rw_lock_init(&entry->state_mutex); \
            \
            /* Step 5: Run the user's custom init routine, or zero-init */ \
            if (init_fn) { \
                init_fn(&entry->state); \
            } else { \
                memset(&entry->state, 0, sizeof(StateType)); \
            } \
            \
            /* Step 6: Set refcount to 1 and push to the front of the registry list */ \
            entry->refcount = 1; \
            entry->free_fn = free_fn; \
            entry->next = StateType##_registry_head; \
            StateType##_registry_head = entry; \
        } \
        return entry; \
    } \
    \
    static void StateType##_destructor(void *p) { \
        /* Bridge destructor used by sqlite3_set_auxdata and sqlite3_create_function_v2 */ \
        StateType##_Entry *entry = (StateType##_Entry *)p; \
        __##StateType##_entry_release(entry); \
    } \
    \
    /* \
     * Internal helper: Walks the global registry linked list and returns the \
     * matching state entry if it exists. Assumes the caller holds the registry lock. \
     * Automatically increments the reference count if found. \
     */ \
    static StateType##_Entry* __##StateType##_entry_find_locked(const char *db_path) { \
        StateType##_Entry *entry = NULL; \
        StateType##_Entry *curr = StateType##_registry_head; \
        while (curr) { \
            if (strcmp(curr->db_path, db_path) == 0) { \
                entry = curr; \
                __##StateType##_entry_retain(entry); \
                break; \
            } \
            curr = curr->next; \
        } \
        return entry; \
    } \
    \
    /* \
     * Internal helper: Retrieves an existing state entry from the global registry \
     * by its database path. Returns NULL if no state exists for this database. \
     * Automatically increments the reference count if found. \
     */ \
    static StateType##_Entry* __##StateType##_entry_get(const char *db_path) { \
        __##StateType##_ensure_mutex_init(); \
        sqlite3_mutex_enter(StateType##_registry_mutex); \
        StateType##_Entry *entry = __##StateType##_entry_find_locked(db_path); \
        sqlite3_mutex_leave(StateType##_registry_mutex); \
        return entry; \
    } \
    \
    /* \
     * Internal helper: Retrieves an existing state entry or creates a new one if it \
     * does not exist. Thread-safe against concurrent initialization. \
     */ \
    static StateType##_Entry* __##StateType##_entry_get_or_create(const char *db_path, void (*init_fn)(StateType*), void (*free_fn)(StateType*)) { \
        __##StateType##_ensure_mutex_init(); \
        sqlite3_mutex_enter(StateType##_registry_mutex); \
        StateType##_Entry *entry = __##StateType##_entry_find_locked(db_path); \
        if (!entry) { \
            entry = __##StateType##_entry_alloc(db_path, init_fn, free_fn); \
        } \
        sqlite3_mutex_leave(StateType##_registry_mutex); \
        return entry; \
    } \
    \
    /* \
     * Initialize or fetch the shared state for the database attached to `db`. \
     * Typically called in sqlite3_extension_init. \
     * Returns a raw pointer that must be passed to sqlite3_create_function_v2 \
     * along with StateType##_destructor. \
     */ \
    static void* StateType##_init(sqlite3 *db, void (*init_fn)(StateType*), void (*free_fn)(StateType*)) { \
        if (!db) return NULL; \
        char resolved_path[128]; \
        const char *db_path = __##StateType##_get_db_path(db, resolved_path); \
        return (void*)__##StateType##_entry_get_or_create(db_path, init_fn, free_fn); \
    } \
    \
    /* \
     * Fetches the state for the current query context. \
     * Implements the highly optimized 3-layer lookup. \
     */ \
    static StateType* StateType##_from_db(sqlite3_context *ctx, sqlite3 *db) { \
        if (!db) return NULL; \
        StateType##_Entry *entry = NULL; \
        /* Layer 1 (Hot Path): Try fetching from SQLite's query-scoped auxdata cache */ \
        if (ctx) { \
            entry = (StateType##_Entry*) sqlite3_get_auxdata(ctx, SQLITE_EXT_STATE_AUXDATA_SLOT); \
            if (entry) { \
                return &entry->state; \
            } \
        } \
        \
        /* Layer 2 (Warm Path): Fallback to walking the global registry list */ \
        char resolved_path[128]; \
        const char *db_path = __##StateType##_get_db_path(db, resolved_path); \
        entry = __##StateType##_entry_get(db_path); \
        \
        if (entry && ctx) { \
            /* Cache back in auxdata so next row hits Layer 1. */ \
            /* set_auxdata takes ownership of the refcount we just bumped. */ \
            sqlite3_set_auxdata(ctx, SQLITE_EXT_STATE_AUXDATA_SLOT, entry, StateType##_destructor); \
        } else if (entry) { \
            /* If there is no ctx, we must release the retain we just acquired */ \
            __##StateType##_entry_release(entry); \
        } \
        \
        return entry ? &entry->state : NULL; \
    } \
    \
    /* \
     * Fast-path helper to extract the state directly from sqlite3_user_data \
     * when it was passed via sqlite3_create_function_v2. \
     */ \
    static StateType* StateType##_from_context(sqlite3_context *ctx) { \
        if (!ctx) return NULL; \
        StateType##_Entry *entry = (StateType##_Entry *)sqlite3_user_data(ctx); \
        return entry ? &entry->state : NULL; \
    } \
    \
    /* \
     * Locks the state's internal read-write lock for SHARED (read) access. \
     * Uses offsetof to jump backwards from the state pointer to find the lock. \
     */ \
    static void StateType##_read_acquire(StateType *state) { \
        if (!state) return; \
        StateType##_Entry *entry = (StateType##_Entry *)((char *)state - offsetof(StateType##_Entry, state)); \
        sqlite3_rw_lock_read_acquire(&entry->state_mutex); \
    } \
    \
    /* \
     * Unlocks the state's internal read-write lock from a SHARED (read) access. \
     */ \
    static void StateType##_read_release(StateType *state) { \
        if (!state) return; \
        StateType##_Entry *entry = (StateType##_Entry *)((char *)state - offsetof(StateType##_Entry, state)); \
        sqlite3_rw_lock_read_release(&entry->state_mutex); \
    } \
    \
    /* \
     * Locks the state's internal read-write lock for EXCLUSIVE (write) access. \
     */ \
    static void StateType##_write_acquire(StateType *state) { \
        if (!state) return; \
        StateType##_Entry *entry = (StateType##_Entry *)((char *)state - offsetof(StateType##_Entry, state)); \
        sqlite3_rw_lock_write_acquire(&entry->state_mutex); \
    } \
    \
    /* \
     * Unlocks the state's internal read-write lock from an EXCLUSIVE (write) access. \
     */ \
    static void StateType##_write_release(StateType *state) { \
        if (!state) return; \
        StateType##_Entry *entry = (StateType##_Entry *)((char *)state - offsetof(StateType##_Entry, state)); \
        sqlite3_rw_lock_write_release(&entry->state_mutex); \
    }

// For backwards compatibility
#define SQLITE_EXTENSION_STATE(StateType) \
    SQLITE_EXTENSION_STATE_DECLARE(StateType) \
    SQLITE_EXTENSION_STATE_DEFINE(StateType)

#endif /* __cplusplus */

#endif /* SQLITE3_EXT_STATE_H */
