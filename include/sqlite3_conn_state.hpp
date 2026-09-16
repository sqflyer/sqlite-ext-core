/**
 * @file sqlite3_conn_state.hpp
 * @brief High-performance, lock-free per-connection unique state registry for SQLite extensions (C++ Template API).
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
 * 2. Lock-Free Query Execution:
 *    Because SQLite enforces single-threaded execution per connection handle
 *    (`sqlite3*`), lookups and mutations during query execution are 100% lock-free.
 * 
 * 3. Multi-Function Reference Counting:
 *    Correctly coordinates lifecycle when multiple UDFs/TVFs on the same connection
 *    share the same connection state, preventing double-free upon `sqlite3_close`.
 * 
 * 4. 2-Tier Caching Pipeline:
 *    - Tier 1: `sqlite3_get_auxdata` fast path on slot 0x45585401.
 *    - Tier 2: `SqlitePtrMap` lookup by `sqlite3*` handle.
 * 
 * 5. C++ RAII Lifecycle:
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
 *     std::string user_token;
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
#include "sqlite3_ext_state.hpp"
#include "stl/duo_alloc.hpp"
#include "stl/duo_hash.hpp"
#include "sqlite3_allocator.hpp"

#ifndef SQLITE_CONN_STATE_FWD_DECLARED
#define SQLITE_CONN_STATE_FWD_DECLARED
template <typename T>
class SqliteConnState;
#endif

/**
 * @brief Thread-safe (registration) & lock-free (execution) per-connection state manager.
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
        sqlite3 *db = nullptr; /**< Associated SQLite database connection handle. */
        int refcount = 0;      /**< Active reference count across UDFs and auxdata bindings. */
        T state;               /**< User-defined state payload instance. */
    };

    /** @brief Global mapping from sqlite3* connection pointers to state entries. */
    static duo::HashMap<sqlite3*, Entry*> registry_map;

    /** @brief Mutex protecting the registry map during initial allocation and destruction. */
    static sqlite3_mutex* registry_mutex;

    /**
     * @brief Lazily initializes the registry mutex using double-checked locking on STATIC_MASTER.
     */
    static void ensure_mutex_init() {
        if (!registry_mutex) {
            sqlite3_mutex *master = sqlite3_mutex_alloc(SQLITE_MUTEX_STATIC_MASTER);
            if (master) sqlite3_mutex_enter(master);
            if (!registry_mutex) {
                registry_mutex = sqlite3_mutex_alloc(SQLITE_MUTEX_STATIC_APP2);
            }
            if (master) sqlite3_mutex_leave(master);
        }
    }

    /**
     * @brief Atomically increments the entry's reference counter.
     */
    static Entry* entry_retain(Entry *entry) {
        if (!entry) return nullptr;
        sqlite_atomic_increment_32(&entry->refcount);
        return entry;
    }

    /**
     * @brief Unlinks from the map and destroys the entry if reference count is zero.
     */
    static void entry_free(Entry *entry) {
        ensure_mutex_init();
        if (registry_mutex) sqlite3_mutex_enter(registry_mutex);
        
        if (sqlite_atomic_load_32(&entry->refcount) > 0) {
            if (registry_mutex) sqlite3_mutex_leave(registry_mutex);
            return;
        }
        
        registry_map.erase(entry->db);
        if (registry_mutex) sqlite3_mutex_leave(registry_mutex);
        
        sqlite_delete(entry);
    }

    /**
     * @brief Atomically decrements the reference counter, triggering teardown at zero.
     */
    static void entry_release(Entry *entry) {
        if (!entry) return;
        if (sqlite_atomic_decrement_32(&entry->refcount) == 0) {
            entry_free(entry);
        }
    }

    /**
     * @brief Dynamically allocates a new entry on the SQLite heap and inserts into registry_map.
     */
    static Entry* entry_alloc(sqlite3 *db, void (*init_fn)(T*)) {
        Entry *entry = sqlite_new<Entry>();
        if (entry) {
            entry->db = db;
            entry->refcount = 1;
            if (init_fn) {
                init_fn(&entry->state);
            }
            registry_map.insert_or_assign(db, entry);
        }
        return entry;
    }

public:
    /**
     * @brief Bridge destructor passed to sqlite3_create_function_v2 (xDestroy).
     * @param p Pointer to the managed Entry.
     */
    static void destructor(void *p) {
        Entry *entry = (Entry *)p;
        entry_release(entry);
    }

    /**
     * @brief Retrieves the strongly-typed T* state for the connection, creating it if not present.
     * 
     * @param db The SQLite database connection handle.
     * @param init_fn Optional initialization callback executed on first creation.
     * @return T* Pointer to the connection state, or nullptr on allocation failure.
     */
    static T* get_or_create(sqlite3 *db, void (*init_fn)(T*) = nullptr) {
        if (!db) return nullptr;
        ensure_mutex_init();
        if (registry_mutex) sqlite3_mutex_enter(registry_mutex);
        
        Entry** p_entry = registry_map.get(db);
        Entry* entry = p_entry ? *p_entry : nullptr;
        if (!entry) {
            entry = entry_alloc(db, init_fn);
        } else {
            entry_retain(entry);
        }
        
        if (registry_mutex) sqlite3_mutex_leave(registry_mutex);
        return entry ? &entry->state : nullptr;
    }

    /**
     * @brief Retrieves an existing strongly-typed T* state for the connection if present.
     * 
     * @param db The SQLite database connection handle.
     * @return T* Pointer to existing state, or nullptr if not registered.
     */
    static T* get(sqlite3 *db) {
        if (!db) return nullptr;
        ensure_mutex_init();
        if (registry_mutex) sqlite3_mutex_enter(registry_mutex);
        
        Entry** p_entry = registry_map.get(db);
        Entry* entry = p_entry ? *p_entry : nullptr;
        
        if (registry_mutex) sqlite3_mutex_leave(registry_mutex);
        return entry ? &entry->state : nullptr;
    }

    /**
     * @brief Allocates and initializes state on connection open, returning a raw void* suitable for pApp.
     * 
     * @param db The SQLite database connection handle.
     * @param init_fn Optional initialization callback.
     * @return void* Raw pointer to the internal Entry to pass as user_data.
     */
    static void* init(sqlite3 *db, void (*init_fn)(T*) = nullptr) {
        if (!db) return nullptr;
        ensure_mutex_init();
        if (registry_mutex) sqlite3_mutex_enter(registry_mutex);
        
        Entry** p_entry = registry_map.get(db);
        Entry* entry = p_entry ? *p_entry : nullptr;
        if (entry) {
            entry_retain(entry);
        } else {
            entry = entry_alloc(db, init_fn);
        }
        
        if (registry_mutex) sqlite3_mutex_leave(registry_mutex);
        return entry;
    }

    /**
     * @brief Fast-path state resolution inside scalar functions and virtual tables.
     * 
     * Pipeline:
     * 1. Layer 1 (Hot Path): Checks AuxData cache on slot 0x45585401 (nanosecond O(1)).
     * 2. Layer 2 (Warm Path): Checks function user_data and populates AuxData cache.
     * 3. Layer 3 (Cold Path): Looks up connection pointer in duo::HashMap hash map.
     * 
     * @param ctx SQLite function invocation context.
     * @return T* Pointer to the connection state, or nullptr if not found.
     */
    static T* from_context(sqlite3_context *ctx) {
        if (!ctx) return nullptr;
        
        // Layer 1 (Hot Path): Auxdata Cache (O(1))
        Entry *entry = (Entry*)sqlite3_get_auxdata(ctx, SQLITE_CONN_STATE_AUXDATA_SLOT);
        if (entry) {
            return &entry->state;
        }
        
        // Layer 2 (Warm Path): Function User Data
        entry = (Entry*)sqlite3_user_data(ctx);
        if (entry) {
            entry_retain(entry);
            sqlite3_set_auxdata(ctx, SQLITE_CONN_STATE_AUXDATA_SLOT, entry, destructor);
            return &entry->state;
        }
        
        // Layer 3 (Cold Path): Hash Table Lookup by sqlite3*
        sqlite3 *db = sqlite3_context_db_handle(ctx);
        return get(db);
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
        return from_context(ctx.handle());
    }

    /**
     * @brief Explicitly removes and destroys the state entry for a connection.
     * 
     * @param db The SQLite database connection handle.
     */
    static void remove(sqlite3 *db) {
        if (!db) return;
        ensure_mutex_init();
        if (registry_mutex) sqlite3_mutex_enter(registry_mutex);
        Entry** p_entry = registry_map.get(db);
        Entry* entry = p_entry ? *p_entry : nullptr;
        if (entry) {
            registry_map.erase(db);
            if (registry_mutex) sqlite3_mutex_leave(registry_mutex);
            sqlite_delete(entry);
            return;
        }
        if (registry_mutex) sqlite3_mutex_leave(registry_mutex);
    }
};

// Static storage definition per instantiated state type
template <typename T>
duo::HashMap<sqlite3*, typename SqliteConnState<T>::Entry*> SqliteConnState<T>::registry_map;

template <typename T>
sqlite3_mutex* SqliteConnState<T>::registry_mutex = nullptr;

/**
 * @brief Zero-dependency C++ template for combined hybrid state management.
 * 
 * Packages both shared per-database state (`SqliteExtState`) and unique per-connection
 * state (`SqliteConnState`) into a single unified container with single-destructor bridging.
 * 
 * @tparam ExtT The user-defined state type shared across all connections to the same database file.
 * @tparam ConnT The user-defined state type private to each individual connection handle (sqlite3*).
 * @tparam LockPolicy Concurrency lock policy for the shared component (defaults to SqliteRwLock).
 */
template <typename ExtT, typename ConnT, typename LockPolicy = SqliteRwLock>
class SqliteHybridState {
public:
    /**
     * @brief Value-type packaging pointers to both shared and connection state.
     * Supports structured bindings (e.g. `auto [ext, conn] = ...`).
     */
    struct State {
        ExtT  *ext  = nullptr; /**< Pointer to shared per-database state (requires lock). */
        ConnT *conn = nullptr; /**< Pointer to unique per-connection state (lock-free). */

        explicit operator bool() const noexcept {
            return ext != nullptr && conn != nullptr;
        }
    };

    /**
     * @brief Internal carrier holding raw entry pointers for single xDestroy bridging.
     */
    struct Holder {
        void *ext_raw  = nullptr;
        void *conn_raw = nullptr;
    };

    /**
     * @brief Initializes both states on the connection and returns a single unified Holder for pApp.
     */
    static void* init(
        sqlite3 *db,
        void (*init_ext)(ExtT*)   = nullptr,
        void (*init_conn)(ConnT*) = nullptr
    ) {
        if (!db) return nullptr;

        void *ext_raw  = SqliteExtState<ExtT, LockPolicy>::init(db, init_ext);
        void *conn_raw = SqliteConnState<ConnT>::init(db, init_conn);

        Holder *holder = static_cast<Holder*>(sqlite3_malloc64(sizeof(Holder)));
        if (holder) {
            holder->ext_raw  = ext_raw;
            holder->conn_raw = conn_raw;
        }
        return static_cast<void*>(holder);
    }

    /**
     * @brief Unified destructor passed as xDestroy to sqlite3_create_function_v2.
     */
    static void destructor(void *p) {
        Holder *holder = static_cast<Holder*>(p);
        if (holder) {
            if (holder->ext_raw) {
                SqliteExtState<ExtT, LockPolicy>::destructor(holder->ext_raw);
            }
            if (holder->conn_raw) {
                SqliteConnState<ConnT>::destructor(holder->conn_raw);
            }
            sqlite3_free(holder);
        }
    }

    /**
     * @brief Resolves both states in O(1) time inside UDFs / TVFs.
     */
    static State from_context(sqlite3_context *ctx) {
        if (!ctx) return State{};

        Holder *holder = static_cast<Holder*>(sqlite3_user_data(ctx));
        if (holder) {
            ExtT *ext = SqliteExtState<ExtT, LockPolicy>::from_ptr(holder->ext_raw);
            ConnT *conn = SqliteConnState<ConnT>::from_ptr(holder->conn_raw);
            return State{ext, conn};
        }

        // Fallback for context without Holder in user_data (e.g. virtual tables or custom UDFs):
        sqlite3 *db = sqlite3_context_db_handle(ctx);
        ExtT *ext = SqliteExtState<ExtT, LockPolicy>::from_db(ctx, db);
        ConnT *conn = SqliteConnState<ConnT>::from_context(ctx);
        return State{ext, conn};
    }

    /**
     * @brief Overload for SqliteContext wrapper instances.
     */
    template <typename Ctx>
    static State from_context(Ctx& ctx) {
        return from_context(ctx.handle());
    }

    /**
     * @brief Resolves both states directly from a sqlite3* database connection handle.
     */
    static State from_db(sqlite3 *db) {
        if (!db) return State{};
        ExtT *ext = SqliteExtState<ExtT, LockPolicy>::get(db);
        ConnT *conn = SqliteConnState<ConnT>::get(db);
        return State{ext, conn};
    }

    /**
     * @brief Acquires write lock on the shared extension state component.
     */
    static void write_acquire(State& s) {
        if (s.ext) SqliteExtState<ExtT, LockPolicy>::write_acquire(s.ext);
    }

    /**
     * @brief Releases write lock on the shared extension state component.
     */
    static void write_release(State& s) {
        if (s.ext) SqliteExtState<ExtT, LockPolicy>::write_release(s.ext);
    }

    /**
     * @brief Acquires read lock on the shared extension state component.
     */
    static void read_acquire(State& s) {
        if (s.ext) SqliteExtState<ExtT, LockPolicy>::read_acquire(s.ext);
    }

    /**
     * @brief Releases read lock on the shared extension state component.
     */
    static void read_release(State& s) {
        if (s.ext) SqliteExtState<ExtT, LockPolicy>::read_release(s.ext);
    }

    /**
     * @brief RAII guard for acquiring and automatically releasing an EXCLUSIVE (write) lock.
     */
    class WriteGuard {
    private:
        typename SqliteExtState<ExtT, LockPolicy>::WriteGuard guard_;
    public:
        explicit WriteGuard(ExtT *ext) : guard_(ext) {}
        explicit WriteGuard(const State &s) : guard_(s.ext) {}
        explicit WriteGuard(const State *s) : guard_(s ? s->ext : nullptr) {}

        ExtT* get() noexcept { return guard_.get(); }
        ExtT* operator->() noexcept { return guard_.operator->(); }
        ExtT& operator*() noexcept { return guard_.operator*(); }
    };

    /**
     * @brief RAII guard for acquiring and automatically releasing a SHARED (read) lock.
     */
    class ReadGuard {
    private:
        typename SqliteExtState<ExtT, LockPolicy>::ReadGuard guard_;
    public:
        explicit ReadGuard(ExtT *ext) : guard_(ext) {}
        explicit ReadGuard(const State &s) : guard_(s.ext) {}
        explicit ReadGuard(const State *s) : guard_(s ? s->ext : nullptr) {}

        ExtT* get() noexcept { return guard_.get(); }
        ExtT* operator->() noexcept { return guard_.operator->(); }
        ExtT& operator*() noexcept { return guard_.operator*(); }
    };
};

template <typename ExtT, typename ConnT, typename LockPolicy = SqliteRwLock>
using SqliteHybrid = SqliteHybridState<ExtT, ConnT, LockPolicy>;

#endif // SQLITE3_CONN_STATE_HPP

