/**
 * @file sqlite3_conn_state.hpp
 * @brief High-performance, thread-safe per-connection state registry for SQLite extensions (C++ Template API).
 * 
 * Provides `SqliteConnState<T>`, an O(1) pointer-hash-map backed per-connection
 * state registry powered by DuoSTL (`duo::HashMap`).
 * 
 * ============================================================================
 * Key Architectural Characteristics:
 * ============================================================================
 * 1. O(1) Direct Lookup by `sqlite3*`:
 *    Uses an open-addressing pointer hash map (`duo::HashMap<sqlite3*, Entry*>`)
 *    for constant-time state retrieval.
 * 
 * 2. Concurrency & Parallel Connection Safety:
 *    Synchronizes the global registry map with an atomic spinlock (`sqlite3_tiny_lock`),
 *    guaranteeing thread safety when creating, looking up, and tearing down connections
 *    in parallel across worker threads with zero runtime mutex initialization overhead.
 * 
 * 3. Lock-Free Query Execution:
 *    Because SQLite serializes execution per database connection (`sqlite3*`),
 *    query-time state lookups and payload mutations are 100% lock-free with zero mutex overhead.
 * 
 * 4. Atomic Multi-Function Reference Counting:
 *    Uses `SqliteAtomic<uint32_t>` to coordinate lifecycles when multiple UDFs/TVFs on the
 *    same connection share the same state, preventing double-free and use-after-free bugs.
 * 
 * 5. 2-Tier Caching Pipeline:
 *    - Tier 1 (Hot Path): Direct Function User Data (`sqlite3_user_data(ctx)`).
 *    - Tier 2 (Cold Path): Hash Table Lookup by `sqlite3*` handle via `from_db(db)`.
 * 
 * 6. C++ RAII Lifecycle:
 *    Safely manages constructors and destructors of complex embedded C++ types
 *    via `sqlite_new<Entry>()` and `sqlite_delete(entry)`.
 * 
 * ============================================================================
 * Example Usage (C++):
 * ============================================================================
 * @code
 * #include "sqlite3_conn_state.hpp"
 * 
 * struct MySession {
 *     int query_count = 0;
 *     duo::String user_token;
 * };
 * 
 * // Extension init:
 * void* raw_state = SqliteConnState<MySession>::init(db);
 * sqlite3_create_function_v2(db, "my_func", 0, SQLITE_UTF8, raw_state, my_func, NULL, NULL,
 *                            SqliteConnState<MySession>::destructor);
 * 
 * // Inside UDF:
 * static void my_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
 *     MySession* session = SqliteConnState<MySession>::from_context(ctx);
 *     if (session) {
 *         session->query_count++;
 *         sqlite3_result_int(ctx, session->query_count);
 *     }
 * }
 * @endcode
 */
#ifndef SQLITE3_CONN_STATE_HPP
#define SQLITE3_CONN_STATE_HPP

#include "sqlite3_conn_state.h" // For SQLITE_CONN_STATE_AUXDATA_SLOT
#include "sqlite3_tiny_lock.hpp"
#include "stl/duo_alloc.hpp"
#include "stl/duo_hash.hpp"
#include "sqlite3_allocator.hpp"
#include "sqlite3_atomic.hpp"

#ifndef SQLITE_CONN_STATE_FWD_DECLARED
#define SQLITE_CONN_STATE_FWD_DECLARED
template <typename T>
class SqliteConnState;
#endif

/**
 * @brief Thread-safe (registration & execution) per-connection state manager.
 * 
 * @tparam T The user-defined state type to associate with each sqlite3* connection.
 */
template <typename T>
class SqliteConnState {
private:
    /**
     * @brief Internal container storing connection pointer, active refcount, and state payload.
     */
    struct Entry {
        sqlite3 *db = nullptr;              /**< Associated SQLite database connection handle. */
        SqliteAtomic<uint32_t> refcount{0}; /**< Active reference count across UDFs and auxdata bindings. */
        T state;                            /**< User-defined state payload instance. */
    };

    using MapType = duo::HashMap<sqlite3*, Entry*>;

    /** @brief Global mapping from sqlite3* connection pointers to state entries. */
    static MapType registry_map;

    /** @brief Atomic spinlock protecting the registry map during allocation and destruction. */
    static SqliteTinyLock registry_lock_inst;

    /**
     * @brief Acquires the registry lock.
     */
    static void registry_lock() noexcept {
        registry_lock_inst.lock();
    }

    /**
     * @brief Releases the registry lock.
     */
    static void registry_unlock() noexcept {
        registry_lock_inst.unlock();
    }

    /**
     * @brief Atomically increments the entry's reference counter.
     */
    static Entry* entry_retain(Entry *entry) noexcept {
        if (!entry) return nullptr;
        entry->refcount.fetch_add(1);
        return entry;
    }

    /**
     * @brief Unlinks from the map and destroys the entry if reference count is zero.
     */
    static void entry_free(Entry *entry) {
        if (!entry) return;
        registry_lock();
        
        if (entry->refcount.load() > 0) {
            registry_unlock();
            return;
        }
        
        if (entry->db) {
            registry_map.erase(entry->db);
            entry->db = nullptr;
        }
        registry_unlock();
        
        sqlite_delete(entry);
    }

    /**
     * @brief Atomically decrements the reference counter, triggering teardown at zero.
     */
    static void entry_release(Entry *entry) {
        if (!entry) return;
        if (entry->refcount.fetch_sub(1) == 1) {
            entry_free(entry);
        }
    }

    /**
     * @brief Looks up existing state entry in the DuoSTL hash map.
     * Assumes the caller holds the registry lock. Does not retain.
     */
    static Entry* entry_find_locked(sqlite3 *db) {
        Entry **p_entry = registry_map.get(db);
        return p_entry ? *p_entry : nullptr;
    }

    /**
     * @brief Retrieves an existing state entry from the global registry by connection handle.
     * Thread-safely locks the registry and automatically retains the entry if found.
     */
    static Entry* entry_get(sqlite3 *db) {
        if (!db) return nullptr;
        registry_lock();
        Entry *entry = entry_find_locked(db);
        if (entry) {
            entry_retain(entry);
        }
        registry_unlock();
        return entry;
    }

public:
    /**
     * @brief Bridge destructor passed to sqlite3_create_function_v2 (xDestroy).
     * @param p Pointer to the managed Entry.
     */
    static void destructor(void *p) {
        Entry *entry = static_cast<Entry*>(p);
        entry_release(entry);
    }

    /**
     * @brief Retrieves an existing strongly-typed T* state for the connection if present.
     * 
     * @param db The SQLite database connection handle.
     * @return T* Pointer to existing state, or nullptr if not registered.
     */
    static T* get(sqlite3 *db) {
        if (!db) return nullptr;
        registry_lock();
        Entry *entry = entry_find_locked(db);
        registry_unlock();
        return entry ? &entry->state : nullptr;
    }

    /**
     * @brief Attempts to retrieve an existing strongly-typed connection state, returning SqliteResult.
     */
    static SqliteResult<T*> try_get(sqlite3 *db) {
        if (!db) {
            return SqliteResult<T*>::err(SQLITE_MISUSE, "Null database connection in SqliteConnState::try_get");
        }
        T* state = get(db);
        if (!state) {
            return SqliteResult<T*>::err(SQLITE_NOTFOUND, "Connection state not registered for database connection");
        }
        return SqliteResult<T*>::ok(state);
    }

    /**
     * @brief Allocates and initializes state on connection open, returning a raw void* suitable for pApp.
     * 
     * Thread-safely coordinates parallel connection creation across worker threads without holding locks
     * during state object construction or init_fn execution.
     * 
     * @param db The SQLite database connection handle.
     * @param init_fn Optional initialization callback.
     * @return void* Raw pointer to the internal Entry to pass as user_data.
     */
    static void* init(sqlite3 *db, void (*init_fn)(T*) = nullptr) {
        if (!db) return nullptr;

        // Fast path: check if already registered
        registry_lock();
        Entry* entry = entry_find_locked(db);
        if (entry) {
            entry_retain(entry);
            registry_unlock();
            return entry;
        }
        registry_unlock();

        // Allocate and construct new entry outside the lock to eliminate lock contention
        Entry* new_entry = sqlite_new<Entry>();
        if (!new_entry) return nullptr;
        new_entry->db = db;
        new_entry->refcount.store(1);
        if (init_fn) {
            init_fn(&new_entry->state);
        }

        // Insert into registry map under mutex
        registry_lock();
        entry = entry_find_locked(db);
        if (entry) {
            // Another thread registered this connection concurrently: adopt existing
            entry_retain(entry);
            registry_unlock();
            sqlite_delete(new_entry);
            return entry;
        }

        if (!registry_map.insert_or_assign(db, new_entry)) {
            registry_unlock();
            sqlite_delete(new_entry);
            return nullptr;
        }
        registry_unlock();
        return new_entry;
    }

    /**
     * @brief Attempts to initialize connection state, returning raw handle in SqliteResult.
     */
    static SqliteResult<void*> try_init(sqlite3 *db, void (*init_fn)(T*) = nullptr) {
        if (!db) {
            return SqliteResult<void*>::err(SQLITE_MISUSE, "Null database connection in SqliteConnState::try_init");
        }
        void* raw = init(db, init_fn);
        if (!raw) {
            return SqliteResult<void*>::nomem("Failed to allocate connection state in SqliteConnState::try_init");
        }
        return SqliteResult<void*>::ok(raw);
    }

    /**
     * @brief Retrieves the state pointer directly from a sqlite3* database handle.
     */
    static T* from_db(sqlite3 *db) {
        return get(db);
    }


    /**
     * @brief Fast-path state resolution inside scalar functions, aggregates, and virtual tables.
     * 
     * Pipeline:
     * 1. Layer 1 (Hot Path): Direct function user_data (`sqlite3_user_data(ctx)`).
     * 2. Layer 2 (Cold Path): Hash table lookup by `sqlite3*` via `from_db(db)`.
     * 
     * @param ctx SQLite function invocation context.
     * @return T* Pointer to the connection state, or nullptr if not found.
     */
    static T* from_context(sqlite3_context *ctx) {
        if (!ctx) return nullptr;
        
        // Layer 1 (Hot Path): Direct Function User Data
        Entry *entry = (Entry*)sqlite3_user_data(ctx);
        if (entry) {
            return &entry->state;
        }
        
        // Layer 2 (Cold Path): Hash Table Lookup by sqlite3*
        sqlite3 *db = sqlite3_context_db_handle(ctx);
        return from_db(db);
    }

    /**
     * @brief Fast-path fetch from raw Entry pointer.
     */
    static T* from_ptr(void *p) {
        if (!p) return nullptr;
        Entry *entry = static_cast<Entry*>(p);
        return &entry->state;
    }

    /**
     * @brief Overload for SqliteContext wrapper instances.
     */
    template <typename Ctx>
    static T* from_context(Ctx& ctx) {
        void* data = ctx.user_data();
        if (data) {
            Entry *entry = static_cast<Entry*>(data);
            return &entry->state;
        }
        return from_context(ctx.get());
    }

    /**
     * @brief Explicitly removes and unregisters the state entry for a connection.
     * 
     * Erases the connection from registry_map and safely decrements its refcount.
     * If no active UDF references remain, releases heap memory immediately.
     * 
     * @param db The SQLite database connection handle.
     */
    static void remove(sqlite3 *db) {
        if (!db) return;
        registry_lock();
        Entry *entry = entry_find_locked(db);
        if (entry) {
            registry_map.erase(db);
            entry->db = nullptr;
            registry_unlock();
            entry_release(entry);
            return;
        }
        registry_unlock();
    }
};

// Static storage definition per instantiated state type
template <typename T>
typename SqliteConnState<T>::MapType SqliteConnState<T>::registry_map;

template <typename T>
SqliteTinyLock SqliteConnState<T>::registry_lock_inst;

// Forward backward-compatibility include for unified hybrid state
#include "sqlite3_hybrid_state.hpp"

#endif // SQLITE3_CONN_STATE_HPP
