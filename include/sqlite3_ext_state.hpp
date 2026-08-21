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
 * - Embedded C++ Objects: Automatically calls Placement New and Pseudo-Destructors for `T`.
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

#include "sqlite3_ext_state.h" // Required for EXT_RWLOCK macros and SQLITE_EXT_STATE_AUXDATA_SLOT
#include <new>

/**
 * @brief Zero-dependency C++ template for per-database shared extension state.
 * 
 * Replaces the SQLITE_EXTENSION_STATE(T) macro by using C++ template mechanics
 * to isolate static state registries perfectly per-type.
 */
template <typename T>
class SqliteExtState {
private:
    struct Entry {
        char *db_path;
        int refcount;
        sqlite3_mutex *ref_mutex;
        ext_rwlock_t state_mutex;
        void (*free_fn)(T*);
        Entry *next;
        T state;
    };

    static Entry* registry_head;
    static sqlite3_mutex* registry_mutex;

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
                registry_mutex = sqlite3_mutex_alloc(SQLITE_MUTEX_STATIC_APP1);
            }
            sqlite3_mutex_leave(master);
        }
    }

    /**
     * @brief Thread-safely increments the reference count of a state entry.
     */
    static Entry* entry_retain(Entry *entry) {
        if (!entry) return nullptr;
        sqlite3_mutex_enter(entry->ref_mutex);
        entry->refcount++;
        sqlite3_mutex_leave(entry->ref_mutex);
        return entry;
    }

    /**
     * @brief Safely destroys a state entry.
     * Handles double-checked locking to prevent race conditions during deletion,
     * removes the entry from the global linked list, triggers the C++ destructor
     * or custom free function, and frees all associated memory and locks.
     */
    static void entry_free(Entry *entry) {
        ensure_mutex_init();
        sqlite3_mutex_enter(registry_mutex);
        
        sqlite3_mutex_enter(entry->ref_mutex);
        if (entry->refcount > 0) {
            sqlite3_mutex_leave(entry->ref_mutex);
            sqlite3_mutex_leave(registry_mutex);
            return;
        }
        sqlite3_mutex_leave(entry->ref_mutex);
        
        Entry **curr = &registry_head;
        while (*curr) {
            if (*curr == entry) {
                *curr = entry->next;
                break;
            }
            curr = &(*curr)->next;
        }
        sqlite3_mutex_leave(registry_mutex);
        
        EXT_RWLOCK_DESTROY(entry->state_mutex);
        
        if (entry->free_fn) {
            entry->free_fn(&entry->state);
        } else {
            entry->state.~T(); // Run C++ destructor if no custom free function provided
        }
        
        if (entry->db_path) sqlite3_free(entry->db_path);
        if (entry->ref_mutex) sqlite3_mutex_free(entry->ref_mutex);
        sqlite3_free(entry);
    }

    /**
     * @brief Thread-safely decrements the reference count.
     * Automatically triggers entry_free() when the count drops to zero.
     */
    static void entry_release(Entry *entry) {
        if (!entry) return;
        sqlite3_mutex_enter(entry->ref_mutex);
        entry->refcount--;
        int count = entry->refcount;
        sqlite3_mutex_leave(entry->ref_mutex);
        
        if (count == 0) {
            entry_free(entry);
        }
    }

    /**
     * @brief Allocates and initializes a new state entry.
     * Configures the internal locks, deep-copies the database path, and uses
     * placement `new` to properly construct the embedded C++ state object.
     * Assumes the caller holds the registry lock, as it injects itself into the linked list.
     */
    static Entry* entry_alloc(const char *db_path, void (*init_fn)(T*), void (*free_fn)(T*)) {
        Entry *entry = (Entry*) sqlite3_malloc(sizeof(Entry));
        if (entry) {
            int path_len = strlen(db_path);
            entry->db_path = (char*)sqlite3_malloc(path_len + 1);
            if (entry->db_path) {
                memcpy(entry->db_path, db_path, path_len + 1);
            } else {
                sqlite3_free(entry);
                entry = nullptr;
            }
        }
        if (entry) {
            entry->ref_mutex = sqlite3_mutex_alloc(SQLITE_MUTEX_FAST);
            if (!entry->ref_mutex) {
                sqlite3_free(entry->db_path);
                sqlite3_free(entry);
                entry = nullptr;
            }
        }
        if (entry) {
            EXT_RWLOCK_INIT(entry->state_mutex);
            
            new (&entry->state) T(); // Placement new for zero-init/constructor
            
            if (init_fn) {
                init_fn(&entry->state);
            }
            
            entry->refcount = 1;
            entry->free_fn = free_fn;
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
        sqlite3_mutex_enter(registry_mutex);
        Entry *entry = entry_find_locked(db_path);
        sqlite3_mutex_leave(registry_mutex);
        return entry;
    }

    /**
     * @brief Retrieves an existing state entry or creates a new one if it does not exist.
     * Thread-safe against concurrent initialization attempts across multiple connections.
     */
    static Entry* entry_get_or_create(const char *db_path, void (*init_fn)(T*), void (*free_fn)(T*)) {
        ensure_mutex_init();
        sqlite3_mutex_enter(registry_mutex);
        Entry *entry = entry_find_locked(db_path);
        if (!entry) {
            entry = entry_alloc(db_path, init_fn, free_fn);
        }
        sqlite3_mutex_leave(registry_mutex);
        return entry;
    }

public:
    /** @brief Bridge destructor used by SQLite to trigger garbage collection. */
    static void destructor(void *p) {
        Entry *entry = (Entry *)p;
        entry_release(entry);
    }

    /** @brief Initializes or fetches the shared state for the attached database. */
    static void* init(sqlite3 *db, void (*init_fn)(T*) = nullptr, void (*free_fn)(T*) = nullptr) {
        if (!db) return nullptr;
        char resolved_path[128];
        const char *db_path = get_db_path(db, resolved_path);
        return (void*)entry_get_or_create(db_path, init_fn, free_fn);
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

    /** @brief Fast-path fetch from context user_data. */
    static T* from_context(sqlite3_context *ctx) {
        if (!ctx) return nullptr;
        Entry *entry = (Entry *)sqlite3_user_data(ctx);
        return entry ? &entry->state : nullptr;
    }

    /**
     * @brief Locks the state's internal read-write lock for SHARED (read) access.
     * Uses offsetof to jump backwards from the user's state pointer to find the hidden lock.
     */
    static void read_acquire(T *state) {
        if (!state) return;
        Entry *entry = (Entry *)((char *)state - offsetof(Entry, state));
        EXT_RWLOCK_READ_ACQUIRE(entry->state_mutex);
    }

    /**
     * @brief Unlocks the state's internal read-write lock from a SHARED (read) access.
     */
    static void read_release(T *state) {
        if (!state) return;
        Entry *entry = (Entry *)((char *)state - offsetof(Entry, state));
        EXT_RWLOCK_READ_RELEASE(entry->state_mutex);
    }

    /**
     * @brief Locks the state's internal read-write lock for EXCLUSIVE (write) access.
     * Uses offsetof to jump backwards from the user's state pointer to find the hidden lock.
     */
    static void write_acquire(T *state) {
        if (!state) return;
        Entry *entry = (Entry *)((char *)state - offsetof(Entry, state));
        EXT_RWLOCK_WRITE_ACQUIRE(entry->state_mutex);
    }

    /**
     * @brief Unlocks the state's internal read-write lock from an EXCLUSIVE (write) access.
     */
    static void write_release(T *state) {
        if (!state) return;
        Entry *entry = (Entry *)((char *)state - offsetof(Entry, state));
        EXT_RWLOCK_WRITE_RELEASE(entry->state_mutex);
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

template <typename T>
typename SqliteExtState<T>::Entry* SqliteExtState<T>::registry_head = nullptr;

template <typename T>
sqlite3_mutex* SqliteExtState<T>::registry_mutex = nullptr;

#endif // SQLITE3_EXT_STATE_HPP
