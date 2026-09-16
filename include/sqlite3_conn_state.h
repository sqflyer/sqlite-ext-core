/**
 * @file sqlite3_conn_state.h
 * @brief High-performance, thread-safe, and memory-safe per-connection state registry for SQLite extensions (Pure C Macro API).
 * 
 * Provides the `SQLITE_CONNECTION_STATE(StateType)` and `DEFINE_SQLITE_CONN_STATE(StateType, Prefix)`
 * macros to generate strongly-typed, isolated per-connection (`sqlite3*`) state registries
 * powered by DuoSTL (`duo_hashmap_t`) O(1) pointer hash maps.
 * 
 * ============================================================================
 * Key Architectural Characteristics:
 * ============================================================================
 * 1. O(1) Direct Lookup:
 *    Maps raw `sqlite3*` connection pointers directly to state structures via
 *    DuoSTL open-addressing pointer hash tables.
 * 
 * 2. Lock-Free Steady-State Execution:
 *    Because SQLite guarantees that an individual connection handle (`sqlite3*`)
 *    is single-threaded during statement execution, reads and mutations inside
 *    active queries require zero mutex or spinlock synchronization.
 * 
 * 3. Multi-Function Reference Counting:
 *    Tracks reference counts across multiple UDF/TVF registrations on the same
 *    connection to prevent double-free or use-after-free bugs when `sqlite3_close`
 *    invokes `xDestroy` on each registered function.
 * 
 * 4. 2-Tier Caching Pipeline:
 *    - Tier 1 (Hot Path): Nanosecond-level `sqlite3_get_auxdata` cache on slot 0x45585401.
 *    - Tier 2 (Warm Path): O(1) DuoSTL pointer map lookup by `sqlite3*`.
 * 
 * 5. 100% SQLite Memory Tracking:
 *    All internal hash tables and state entries allocate exclusively through
 *    `sqlite3_realloc64` and `sqlite3_free`.
 * 
 * ============================================================================
 * Example Usage (Pure C):
 * ============================================================================
 * @code
 * #include "sqlite3ext.h"
 * #include "sqlite3_conn_state.h"
 * 
 * typedef struct {
 *     int query_count;
 *     char session_id[32];
 * } ConnSession;
 * 
 * // Generate API routines: ConnSession_init, ConnSession_from_context, ConnSession_destructor
 * SQLITE_CONNECTION_STATE(ConnSession)
 * 
 * int sqlite3_myext_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi) {
 *     SQLITE_EXTENSION_INIT2(pApi);
 *     void* raw_state = ConnSession_init(db, NULL, NULL);
 *     return sqlite3_create_function_v2(
 *         db, "my_func", 0, SQLITE_UTF8, raw_state, my_func_impl, NULL, NULL, ConnSession_destructor
 *     );
 * }
 * 
 * static void my_func_impl(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
 *     ConnSession *session = ConnSession_from_context(ctx);
 *     if (session) {
 *         session->query_count++;
 *         sqlite3_result_int(ctx, session->query_count);
 *     }
 * }
 * @endcode
 */
#ifndef SQLITE3_CONN_STATE_H
#define SQLITE3_CONN_STATE_H

#include "sqlite3ext.h"
#include <sqlite3.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

#include "stl/duo_alloc.h"
#include "stl/duo_hash.h"
#include "sqlite3_atomic.h"

/**
 * @brief Dedicated SQLite AuxData argument slot for per-connection state caching.
 * Uses a unique high-range integer tag (0x45585401) to avoid colliding with
 * argument index 0 used by SqliteExtState.
 */
#ifndef SQLITE_CONN_STATE_AUXDATA_SLOT
#define SQLITE_CONN_STATE_AUXDATA_SLOT 0x45585401
#endif

/**
 * @brief Macro to define a complete per-connection state registry with custom prefix.
 * 
 * Generates:
 * - `Prefix_ConnEntry`: Internal state container with refcount and destruction hook.
 * - `Prefix_init(db, init_fn, free_fn)`: Initializes connection state and returns `void*`.
 * - `Prefix_from_context(ctx)`: Resolves state via AuxData fast-path -> UserData -> Hash table.
 * - `Prefix_from_db(db)`: Resolves state directly by `sqlite3*` handle.
 * - `Prefix_destructor(p)`: Bridge destructor for `sqlite3_create_function_v2` / `xDestroy`.
 * 
 * @param StateType The C struct type representing the state.
 * @param Prefix The identifier prefix for generated functions and types.
 */
#define DEFINE_SQLITE_CONN_STATE(StateType, Prefix)                                            \
    /** @brief Internal wrapper managing the state payload and reference counter. */            \
    typedef struct Prefix##_ConnEntry {                                                        \
        sqlite3 *db;                 /**< Associated SQLite connection handle. */              \
        int refcount;                /**< Active function/auxdata reference counter. */        \
        void (*free_fn)(StateType*); /**< Optional custom cleanup callback. */                  \
        StateType state;             /**< User-defined state payload. */                       \
    } Prefix##_ConnEntry;                                                                      \
                                                                                               \
    static duo_hashmap_t* Prefix##_conn_map = NULL;                                            \
    static sqlite3_mutex* Prefix##_conn_mutex = NULL;                                          \
                                                                                               \
    /** @brief Lazily initializes the global connection registry mutex using double-checked locking. */ \
    static void Prefix##_ensure_conn_mutex_init(void) {                                        \
        if (!Prefix##_conn_mutex) {                                                            \
            sqlite3_mutex *master = sqlite3_mutex_alloc(SQLITE_MUTEX_STATIC_MASTER);           \
            if (master) sqlite3_mutex_enter(master);                                           \
            if (!Prefix##_conn_mutex) {                                                        \
                Prefix##_conn_mutex = sqlite3_mutex_alloc(SQLITE_MUTEX_STATIC_APP2);           \
            }                                                                                  \
            if (master) sqlite3_mutex_leave(master);                                           \
        }                                                                                      \
    }                                                                                          \
                                                                                               \
    /** @brief Increments the atomic reference counter for the connection entry. */            \
    static Prefix##_ConnEntry* Prefix##_conn_retain(Prefix##_ConnEntry *entry) {               \
        if (!entry) return NULL;                                                               \
        sqlite_atomic_increment_32(&entry->refcount);                                          \
        return entry;                                                                          \
    }                                                                                          \
                                                                                               \
    /** @brief Unlinks and deletes the connection entry when reference count reaches zero. */  \
    static void Prefix##_conn_free(Prefix##_ConnEntry *entry) {                                \
        Prefix##_ensure_conn_mutex_init();                                                     \
        if (Prefix##_conn_mutex) sqlite3_mutex_enter(Prefix##_conn_mutex);                     \
                                                                                               \
        if (sqlite_atomic_load_32(&entry->refcount) > 0) {                                     \
            if (Prefix##_conn_mutex) sqlite3_mutex_leave(Prefix##_conn_mutex);                 \
            return;                                                                            \
        }                                                                                      \
                                                                                               \
        if (Prefix##_conn_map) {                                                               \
            sqlite3 *k = entry->db;                                                            \
            duo_hashmap_delete(Prefix##_conn_map, &k);                                         \
            if (duo_hashmap_count(Prefix##_conn_map) == 0) {                                  \
                duo_hashmap_free(Prefix##_conn_map);                                           \
                Prefix##_conn_map = NULL;                                                      \
            }                                                                                  \
        }                                                                                      \
        if (Prefix##_conn_mutex) sqlite3_mutex_leave(Prefix##_conn_mutex);                     \
                                                                                               \
        if (entry->free_fn) {                                                                  \
            entry->free_fn(&entry->state);                                                     \
        }                                                                                      \
        sqlite3_free(entry);                                                                   \
    }                                                                                          \
                                                                                               \
    /** @brief Decrements the reference counter and triggers teardown at zero. */              \
    static void Prefix##_conn_release(Prefix##_ConnEntry *entry) {                             \
        if (!entry) return;                                                                    \
        if (sqlite_atomic_decrement_32(&entry->refcount) == 0) {                               \
            Prefix##_conn_free(entry);                                                         \
        }                                                                                      \
    }                                                                                          \
                                                                                               \
    /** @brief SQLite bridge destructor callback for xDestroy. */                              \
    static void Prefix##_destructor(void *p) {                                                 \
        Prefix##_ConnEntry *entry = (Prefix##_ConnEntry*)p;                                    \
        Prefix##_conn_release(entry);                                                          \
    }                                                                                          \
                                                                                               \
    /** @brief Retrieves the state pointer directly from a sqlite3* database handle. */        \
    static StateType* Prefix##_from_db(sqlite3 *db) {                                          \
        if (!db) return NULL;                                                                  \
        Prefix##_ensure_conn_mutex_init();                                                     \
        if (Prefix##_conn_mutex) sqlite3_mutex_enter(Prefix##_conn_mutex);                     \
        Prefix##_ConnEntry **p_entry = Prefix##_conn_map ? (Prefix##_ConnEntry**)duo_hashmap_get(Prefix##_conn_map, &db) : NULL; \
        Prefix##_ConnEntry *entry = p_entry ? *p_entry : NULL;                                 \
        if (Prefix##_conn_mutex) sqlite3_mutex_leave(Prefix##_conn_mutex);                     \
        return entry ? &entry->state : NULL;                                                   \
    }                                                                                          \
                                                                                               \
    /** @brief Allocates or retains state for a connection, returning a pointer for pApp. */   \
    static void* Prefix##_init(sqlite3 *db, void (*init_fn)(StateType*), void (*free_fn)(StateType*)) { \
        if (!db) return NULL;                                                                  \
        Prefix##_ensure_conn_mutex_init();                                                     \
        if (Prefix##_conn_mutex) sqlite3_mutex_enter(Prefix##_conn_mutex);                     \
        Prefix##_ConnEntry **p_entry = Prefix##_conn_map ? (Prefix##_ConnEntry**)duo_hashmap_get(Prefix##_conn_map, &db) : NULL; \
        Prefix##_ConnEntry *entry = p_entry ? *p_entry : NULL;                                 \
        if (entry) {                                                                           \
            Prefix##_conn_retain(entry);                                                       \
            if (Prefix##_conn_mutex) sqlite3_mutex_leave(Prefix##_conn_mutex);                 \
            return entry;                                                                      \
        }                                                                                      \
        if (!Prefix##_conn_map) {                                                              \
            Prefix##_conn_map = duo_hashmap_new(sizeof(sqlite3*), sizeof(Prefix##_ConnEntry*), 16, 0, 0, NULL, NULL, NULL, NULL, NULL); \
        }                                                                                      \
        entry = (Prefix##_ConnEntry*)sqlite3_malloc64(sizeof(Prefix##_ConnEntry));             \
        if (entry) {                                                                           \
            memset(entry, 0, sizeof(Prefix##_ConnEntry));                                      \
            entry->db = db;                                                                    \
            entry->refcount = 1;                                                               \
            entry->free_fn = free_fn;                                                          \
            if (init_fn) init_fn(&entry->state);                                               \
            if (Prefix##_conn_map) {                                                           \
                duo_hashmap_set(Prefix##_conn_map, &db, &entry);                               \
            }                                                                                  \
        }                                                                                      \
        if (Prefix##_conn_mutex) sqlite3_mutex_leave(Prefix##_conn_mutex);                     \
        return entry;                                                                          \
    }                                                                                          \
                                                                                               \
    /** @brief Multi-tier fast resolution of connection state inside SQL functions. */         \
    static StateType* Prefix##_from_context(sqlite3_context *ctx) {                            \
        if (!ctx) return NULL;                                                                 \
        Prefix##_ConnEntry *entry = (Prefix##_ConnEntry*)sqlite3_get_auxdata(ctx, SQLITE_CONN_STATE_AUXDATA_SLOT); \
        if (entry) return &entry->state;                                                       \
        entry = (Prefix##_ConnEntry*)sqlite3_user_data(ctx);                                   \
        if (entry) {                                                                           \
            Prefix##_conn_retain(entry);                                                       \
            sqlite3_set_auxdata(ctx, SQLITE_CONN_STATE_AUXDATA_SLOT, entry, Prefix##_destructor); \
            return &entry->state;                                                              \
        }                                                                                      \
        sqlite3 *db = sqlite3_context_db_handle(ctx);                                          \
        return Prefix##_from_db(db);                                                           \
    }

/**
 * @brief Convenience macro generating per-connection state routines where Prefix matches StateType.
 */
#define SQLITE_CONNECTION_STATE(StateType) DEFINE_SQLITE_CONN_STATE(StateType, StateType)

/**
 * @brief Generates a unified hybrid state struct and management routines packaging both
 * a shared extension state (ExtStateType) and a per-connection state (ConnStateType).
 * 
 * Generates:
 * - `HybridPrefix_HybridState`: Struct holding `ExtStateType *ext` and `ConnStateType *conn`.
 * - `HybridPrefix_hybrid_init(db, ext_init, ext_free, conn_init, conn_free)`: Initializes both and returns a single `void*` for `pApp`.
 * - `HybridPrefix_hybrid_destructor(p)`: Single unified destructor callback for `sqlite3_create_function_v2` / `xDestroy`.
 * - `HybridPrefix_hybrid_from_context(ctx)`: Resolves both states from a `sqlite3_context*`.
 * - `HybridPrefix_hybrid_from_db(db)`: Resolves both states from a `sqlite3*` handle.
 * - `HybridPrefix_hybrid_write_acquire/release`: Lock helpers for the shared component.
 * - `HybridPrefix_hybrid_read_acquire/release`: Read lock helpers for the shared component.
 */
#define DEFINE_SQLITE_HYBRID_STATE(ExtStateType, ExtPrefix, ConnStateType, ConnPrefix, HybridPrefix) \
    /** @brief Unified hybrid structure packaging both shared and connection state pointers. */       \
    typedef struct HybridPrefix##_HybridState {                                                       \
        ExtStateType *ext;   /**< Pointer to shared per-database state (requires lock). */            \
        ConnStateType *conn; /**< Pointer to unique per-connection state (lock-free). */              \
    } HybridPrefix##_HybridState;                                                                     \
                                                                                                      \
    /** @brief Internal carrier holding raw entry pointers for single xDestroy bridging. */           \
    typedef struct HybridPrefix##_HybridHolder {                                                      \
        void *ext_raw;  /**< Pointer to ExtState Entry. */                                            \
        void *conn_raw; /**< Pointer to ConnState Entry. */                                           \
    } HybridPrefix##_HybridHolder;                                                                    \
                                                                                                      \
    /** @brief Unified destructor passed as xDestroy to sqlite3_create_function_v2. */                \
    static inline void HybridPrefix##_hybrid_destructor(void *p) {                                    \
        HybridPrefix##_HybridHolder *holder = (HybridPrefix##_HybridHolder*)p;                        \
        if (holder) {                                                                                 \
            if (holder->ext_raw)  ExtPrefix##_destructor(holder->ext_raw);                            \
            if (holder->conn_raw) ConnPrefix##_destructor(holder->conn_raw);                          \
            sqlite3_free(holder);                                                                     \
        }                                                                                             \
    }                                                                                                 \
                                                                                                      \
    /** @brief Initializes both states on the connection and returns a single unified holder. */      \
    static inline void* HybridPrefix##_hybrid_init(                                                   \
        sqlite3 *db,                                                                                  \
        void (*ext_init)(ExtStateType*),                                                              \
        void (*ext_free)(ExtStateType*),                                                              \
        void (*conn_init)(ConnStateType*),                                                            \
        void (*conn_free)(ConnStateType*)                                                             \
    ) {                                                                                               \
        if (!db) return NULL;                                                                         \
        void *ext_raw  = ExtPrefix##_init(db, ext_init, ext_free);                                    \
        void *conn_raw = ConnPrefix##_init(db, conn_init, conn_free);                                 \
        HybridPrefix##_HybridHolder *holder =                                                         \
            (HybridPrefix##_HybridHolder*)sqlite3_malloc64(sizeof(HybridPrefix##_HybridHolder));      \
        if (holder) {                                                                                 \
            holder->ext_raw  = ext_raw;                                                               \
            holder->conn_raw = conn_raw;                                                              \
        }                                                                                             \
        return (void*)holder;                                                                         \
    }                                                                                                 \
                                                                                                      \
    /** @brief Resolves both states into a single HybridState struct inside UDFs / TVFs. */          \
    static inline HybridPrefix##_HybridState HybridPrefix##_hybrid_from_context(sqlite3_context *ctx) {\
        HybridPrefix##_HybridState state;                                                             \
        HybridPrefix##_HybridHolder *holder = (HybridPrefix##_HybridHolder*)sqlite3_user_data(ctx);   \
        if (holder) {                                                                                 \
            ExtPrefix##_Entry *ext_entry = (ExtPrefix##_Entry*)holder->ext_raw;                       \
            ConnPrefix##_ConnEntry *conn_entry = (ConnPrefix##_ConnEntry*)holder->conn_raw;           \
            state.ext  = ext_entry ? &ext_entry->state : NULL;                                        \
            state.conn = conn_entry ? &conn_entry->state : NULL;                                      \
        } else {                                                                                      \
            sqlite3 *db = sqlite3_context_db_handle(ctx);                                             \
            state.ext  = ExtPrefix##_from_db(ctx, db);                                                \
            state.conn = ConnPrefix##_from_context(ctx);                                              \
        }                                                                                             \
        return state;                                                                                 \
    }                                                                                                 \
                                                                                                      \
    /** @brief Resolves both states directly from a sqlite3* database handle. */                      \
    static inline HybridPrefix##_HybridState HybridPrefix##_hybrid_from_db(sqlite3 *db) {             \
        HybridPrefix##_HybridState state;                                                             \
        state.ext  = ExtPrefix##_from_db_handle(db);                                                  \
        state.conn = ConnPrefix##_from_db(db);                                                        \
        return state;                                                                                 \
    }                                                                                                 \
                                                                                                      \
    /** @brief Acquires write lock on the shared extension state component. */                        \
    static inline void HybridPrefix##_hybrid_write_acquire(HybridPrefix##_HybridState *state) {       \
        if (state && state->ext) ExtPrefix##_write_acquire(state->ext);                               \
    }                                                                                                 \
                                                                                                      \
    /** @brief Releases write lock on the shared extension state component. */                        \
    static inline void HybridPrefix##_hybrid_write_release(HybridPrefix##_HybridState *state) {       \
        if (state && state->ext) ExtPrefix##_write_release(state->ext);                               \
    }                                                                                                 \
                                                                                                      \
    /** @brief Acquires read lock on the shared extension state component. */                         \
    static inline void HybridPrefix##_hybrid_read_acquire(HybridPrefix##_HybridState *state) {        \
        if (state && state->ext) ExtPrefix##_read_acquire(state->ext);                                \
    }                                                                                                 \
                                                                                                      \
    /** @brief Releases read lock on the shared extension state component. */                         \
    static inline void HybridPrefix##_hybrid_read_release(HybridPrefix##_HybridState *state) {        \
        if (state && state->ext) ExtPrefix##_read_release(state->ext);                                \
    }

/**
 * @brief Convenience macro for generating a hybrid state registry when ExtPrefix and ConnPrefix match type names.
 */
#define SQLITE_HYBRID_STATE(ExtStateType, ConnStateType, HybridPrefix) \
    DEFINE_SQLITE_HYBRID_STATE(ExtStateType, ExtStateType, ConnStateType, ConnStateType, HybridPrefix)

#endif // SQLITE3_CONN_STATE_H
