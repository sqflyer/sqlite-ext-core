/**
 * @file sqlite3_ext_state.hpp
 * @brief High-performance, thread-safe, and memory-safe shared state registry for SQLite extensions (C++ Template API).
 * 
 * This header provides `SqliteExtState<T>`, a zero-dependency C++ template that generates 
 * a perfectly isolated per-database shared state registry. It replicates the advanced 
 * 3-layer caching and garbage collection architecture of the `sqlite-ext-core` Rust crate,
 * replacing the C macro `SQLITE_EXTENSION_STATE` with clean, type-safe C++ template mechanics.
 * 
 * Features:
 * - O(1) Fast-Path Caching via SQLite's internal auxdata.
 * - Automated Garbage Collection tied to SQLite's connection lifecycle (`xDestroy`).
 * - Cross-Platform Read/Write Locks (Windows SRWLOCK, POSIX pthreads, and WASM).
 * - C++ RAII Guards (`ReadGuard` / `WriteGuard`) for exception-safe locking.
 * - Embedded C++ Objects: Automatically calls `sqlite_construct_at` and `sqlite_destroy_at` for `T`.
 * - In-Memory Database Isolation (`:memory:` instances don't share state).
 * 
 * Usage:
 *   struct SharedState { int counter; std::string name; };
 * 
 *   // In extension init:
 *   void* raw = SqliteExtState<SharedState>::init(db);
 *   sqlite3_create_function_v2(..., raw, ..., SqliteExtState<SharedState>::destructor);
 * 
 *   // In scalar function:
 *   SharedState* state = SqliteExtState<SharedState>::from_context(ctx);
 *   
 *   // Safe RAII Locking:
 *   {
 *       SqliteExtState<SharedState>::WriteGuard lock(state);
 *       lock->counter++;
 *   }
 */
#ifndef SQLITE3_EXT_STATE_HPP
#define SQLITE3_EXT_STATE_HPP

#include "sqlite3_ext_state.h" // Required for SQLITE_EXT_STATE_AUXDATA_SLOT
#include "sqlite3_allocator.hpp"
#include "sqlite3_rw_lock.hpp"
#include "sqlite3_tiny_lock.hpp"
#include "sqlite3_mutex_lock.hpp"

#ifndef SQLITE_EXT_STATE_FWD_DECLARED
#define SQLITE_EXT_STATE_FWD_DECLARED
template <typename T, typename LockPolicy = SqliteRwLock>
class SqliteExtState;
#endif

/**
 * @brief Zero-dependency C++ template for per-database shared extension state.
 * 
 * Replaces the SQLITE_EXTENSION_STATE(T) macro by using C++ template mechanics
 * to isolate static state registries perfectly per-type.
 */
template <typename T, typename LockPolicy>
class SqliteExtState {
private:
    struct Entry {
        char *db_path = nullptr;
        int refcount = 0;
        LockPolicy state_mutex;
        Entry *next = nullptr;
        T state;
        
        ~Entry() {
            if (db_path) sqlite3_free(db_path);
        }
    };

    static Entry* registry_head;
    static SqliteMutex* registry_mutex;

    /**
     * @brief Resolves the database path for use as a registry key.
     * Generates a unique collision-proof string (e.g. ":memory:0x1234") for in-memory databases.
     */
    static const char* get_db_path(sqlite3 *db, char *resolved_path_buf) {
        const char *raw_path = sqlite3_db_filename(db, "main");
        if (!raw_path || raw_path[0] == '\0') {
            snprintf(resolved_path_buf, 128, ":memory:%p", (void*)db);
            return resolved_path_buf;
        }
        return raw_path;
    }

    /**
     * @brief Lazily initializes the global registry mutex.
     * Uses SQLITE_MUTEX_STATIC_MASTER to prevent initialization race conditions.
     */
    static void ensure_mutex_init() {
        if (!registry_mutex) {
            sqlite3_mutex *master = sqlite3_mutex_alloc(SQLITE_MUTEX_STATIC_MASTER);
            sqlite3_mutex_enter(master);
            if (!registry_mutex) {
                registry_mutex = sqlite_new<SqliteMutex>(SQLITE_MUTEX_STATIC_APP1);
            }
            sqlite3_mutex_leave(master);
        }
    }

    /**
     * @brief Thread-safely increments the reference count of a state entry.
     */
    static Entry* entry_retain(Entry *entry) {
        if (!entry) return nullptr;
        sqlite_atomic_increment_32(&entry->refcount);
        return entry;
    }

    /**
     * @brief Safely destroys a state entry.
     * Handles double-checked locking to prevent race conditions during deletion,
     * removes the entry from the global linked list, and safely deletes the entry
     * via sqlite_delete, automatically invoking all C++ destructors.
     */
    static void entry_free(Entry *entry) {
        ensure_mutex_init();
        registry_mutex->lock();
        
        if (sqlite_atomic_load_32(&entry->refcount) > 0) {
            registry_mutex->unlock();
            return;
        }
        
        Entry **curr = &registry_head;
        while (*curr) {
            if (*curr == entry) {
                *curr = entry->next;
                break;
            }
            curr = &(*curr)->next;
        }
        registry_mutex->unlock();
        
        sqlite_delete(entry);
    }

    /**
     * @brief Thread-safely decrements the reference count.
     * Automatically triggers entry_free() when the count drops to zero.
     */
    static void entry_release(Entry *entry) {
        if (!entry) return;
        if (sqlite_atomic_decrement_32(&entry->refcount) == 0) {
            entry_free(entry);
        }
    }

    /**
     * @brief Allocates and initializes a new state entry.
     * Deep-copies the database path and uses `sqlite_new` to properly 
     * construct the embedded C++ state object and locks.
     * Assumes the caller holds the registry lock, as it injects itself into the linked list.
     */
    static Entry* entry_alloc(const char *db_path, void (*init_fn)(T*)) {
        Entry *entry = sqlite_new<Entry>();
        if (entry) {
            int path_len = strlen(db_path);
            entry->db_path = (char*)sqlite3_malloc(path_len + 1);
            if (entry->db_path) {
                memcpy(entry->db_path, db_path, path_len + 1);
            } else {
                sqlite_delete(entry);
                return nullptr;
            }
            
            if (init_fn) {
                init_fn(&entry->state);
            }
            
            entry->refcount = 1;
            entry->next = registry_head;
            registry_head = entry;
        }
        return entry;
    }

    /**
     * @brief Scans the global registry linked list for an existing state entry.
     * Assumes the caller holds the registry lock. Automatically retains the entry if found.
     */
    static Entry* entry_find_locked(const char *db_path) {
        Entry *entry = nullptr;
        Entry *curr = registry_head;
        while (curr) {
            if (strcmp(curr->db_path, db_path) == 0) {
                entry = curr;
                entry_retain(entry);
                break;
            }
            curr = curr->next;
        }
        return entry;
    }

    /**
     * @brief Retrieves an existing state entry from the global registry by its database path.
     * Thread-safely locks the registry and automatically retains the entry if found.
     */
    static Entry* entry_get(const char *db_path) {
        ensure_mutex_init();
        registry_mutex->lock();
        Entry *entry = entry_find_locked(db_path);
        registry_mutex->unlock();
        return entry;
    }

    /**
     * @brief Retrieves an existing state entry or creates a new one if it does not exist.
     * Thread-safe against concurrent initialization attempts across multiple connections.
     */
    static Entry* entry_get_or_create(const char *db_path, void (*init_fn)(T*)) {
        ensure_mutex_init();
        registry_mutex->lock();
        Entry *entry = entry_find_locked(db_path);
        if (!entry) {
            entry = entry_alloc(db_path, init_fn);
        }
        registry_mutex->unlock();
        return entry;
    }

public:
    /** @brief Bridge destructor used by SQLite to trigger garbage collection. */
    static void destructor(void *p) {
        Entry *entry = (Entry *)p;
        entry_release(entry);
    }

    /** 
     * @brief Retrieves the strongly-typed T* shared state for the attached database, creating it if not present.
     * @param db The SQLite database connection.
     * @param init_fn Optional setup callback executed only when the state is created for the first time.
     * @return Strongly-typed pointer to the shared state instance (T*).
     */
    static T* get_or_create(sqlite3 *db, void (*init_fn)(T*) = nullptr) {
        if (!db) return nullptr;
        char resolved_path[128];
        const char *db_path = get_db_path(db, resolved_path);
        Entry *entry = entry_get_or_create(db_path, init_fn);
        return entry ? &entry->state : nullptr;
    }

    /** 
     * @brief Retrieves an existing strongly-typed T* shared state for the attached database if present.
     * @param db The SQLite database connection.
     * @return Strongly-typed pointer T* if found, or nullptr if not yet created.
     */
    static T* get(sqlite3 *db) {
        if (!db) return nullptr;
        char resolved_path[128];
        const char *db_path = get_db_path(db, resolved_path);
        ensure_mutex_init();
        registry_mutex->lock();
        Entry *entry = entry_find_locked(db_path);
        if (entry) {
            entry_release(entry);
        }
        registry_mutex->unlock();
        return entry ? &entry->state : nullptr;
    }

    /** @brief Initializes or fetches the shared state for the attached database, returning raw entry handle. */
    static void* init(sqlite3 *db, void (*init_fn)(T*) = nullptr) {
        if (!db) return nullptr;
        char resolved_path[128];
        const char *db_path = get_db_path(db, resolved_path);
        return (void*)entry_get_or_create(db_path, init_fn);
    }

    /** @brief Fetches state for the current query using the optimized 3-layer lookup. */
    static T* from_db(sqlite3_context *ctx, sqlite3 *db) {
        if (!db) return nullptr;
        Entry *entry = nullptr;
        
        if (ctx) {
            entry = (Entry*) sqlite3_get_auxdata(ctx, SQLITE_EXT_STATE_AUXDATA_SLOT);
            if (entry) {
                return &entry->state;
            }
        }
        
        char resolved_path[128];
        const char *db_path = get_db_path(db, resolved_path);
        entry = entry_get(db_path);
        
        if (entry && ctx) {
            sqlite3_set_auxdata(ctx, SQLITE_EXT_STATE_AUXDATA_SLOT, entry, destructor);
        } else if (entry) {
            entry_release(entry);
        }
        
        return entry ? &entry->state : nullptr;
    }

    /** @brief Fast-path fetch from raw Entry pointer. */
    static T* from_ptr(void *p) {
        if (!p) return nullptr;
        Entry *entry = (Entry *)p;
        return &entry->state;
    }

    /** @brief Fast-path fetch from raw sqlite3_context user_data. */
    static T* from_context(sqlite3_context *ctx) {
        if (!ctx) return nullptr;
        Entry *entry = (Entry *)sqlite3_user_data(ctx);
        return entry ? &entry->state : nullptr;
    }

    /** @brief Fast-path fetch from injected SqliteContext or any context wrapper. */
    template <typename TContext>
    static inline auto from_context(TContext ctx) -> T* {
        void* data = ctx.user_data();
        if (!data) return nullptr;
        Entry *entry = (Entry *)data;
        return &entry->state;
    }

    /**
     * @brief Locks the state's internal read-write lock for SHARED (read) access.
     * Uses offsetof to jump backwards from the user's state pointer to find the hidden lock.
     */
    static void read_acquire(T *state) {
        if (!state) return;
        Entry *entry = (Entry *)((char *)state - offsetof(Entry, state));
        entry->state_mutex.lock_read();
    }

    /**
     * @brief Unlocks the state's internal read-write lock from a SHARED (read) access.
     */
    static void read_release(T *state) {
        if (!state) return;
        Entry *entry = (Entry *)((char *)state - offsetof(Entry, state));
        entry->state_mutex.unlock_read();
    }

    /**
     * @brief Locks the state's internal read-write lock for EXCLUSIVE (write) access.
     * Uses offsetof to jump backwards from the user's state pointer to find the hidden lock.
     */
    static void write_acquire(T *state) {
        if (!state) return;
        Entry *entry = (Entry *)((char *)state - offsetof(Entry, state));
        entry->state_mutex.lock_write();
    }

    /**
     * @brief Unlocks the state's internal read-write lock from an EXCLUSIVE (write) access.
     */
    static void write_release(T *state) {
        if (!state) return;
        Entry *entry = (Entry *)((char *)state - offsetof(Entry, state));
        entry->state_mutex.unlock_write();
    }

    /**
     * @brief RAII guard for acquiring and automatically releasing a SHARED (read) lock.
     * Guarantees exception safety and prevents accidental deadlocks.
     */
    class ReadGuard {
        T* state;
    public:
        explicit ReadGuard(T* s) : state(s) { read_acquire(state); }
        ~ReadGuard() { read_release(state); }
        T* operator->() { return state; }
        T& operator*() { return *state; }
    };

    /**
     * @brief RAII guard for acquiring and automatically releasing an EXCLUSIVE (write) lock.
     * Guarantees exception safety and prevents accidental deadlocks.
     */
    class WriteGuard {
        T* state;
    public:
        explicit WriteGuard(T* s) : state(s) { write_acquire(state); }
        ~WriteGuard() { write_release(state); }
        T* operator->() { return state; }
        T& operator*() { return *state; }
    };
};

template <typename T, typename LockPolicy>
typename SqliteExtState<T, LockPolicy>::Entry* SqliteExtState<T, LockPolicy>::registry_head = nullptr;

template <typename T, typename LockPolicy>
SqliteMutex* SqliteExtState<T, LockPolicy>::registry_mutex = nullptr;

// Convenient Type Aliases for Lock Policies
template <typename T>
using SqliteExtStateRw = SqliteExtState<T, SqliteRwLock>;

template <typename T>
using SqliteExtStateTiny = SqliteExtState<T, SqliteTinyLock>;

template <typename T>
using SqliteExtStateMutex = SqliteExtState<T, SqliteMutex>;

#endif // SQLITE3_EXT_STATE_HPP
