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
 *    - Tier 1 (Hot Path): Nanosecond-level `sqlite3_user_data` direct retrieval (1 pointer dereference, 0 locks).
 *    - Tier 2 (Cold Path): O(1) DuoSTL pointer map lookup by `sqlite3*` handle via `Prefix##_from_db(db)`.
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
#include "sqlite3_tiny_lock.h"

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
    static sqlite3_tiny_lock Prefix##_conn_lock = {0};                                         \
                                                                                               \
    static inline void Prefix##_conn_lock_acquire(void) {                                      \
        sqlite3_tiny_lock_lock(&Prefix##_conn_lock);                                           \
    }                                                                                          \
                                                                                               \
    static inline void Prefix##_conn_lock_release(void) {                                      \
        sqlite3_tiny_lock_unlock(&Prefix##_conn_lock);                                         \
    }                                                                                          \
                                                                                               \
    /** @brief Increments the atomic reference counter for the connection entry. */            \
    static inline Prefix##_ConnEntry* Prefix##_conn_retain(Prefix##_ConnEntry *entry) {        \
        if (!entry) return NULL;                                                               \
        sqlite_atomic_increment_32(&entry->refcount);                                          \
        return entry;                                                                          \
    }                                                                                          \
                                                                                               \
    /** @brief Unlinks and deletes the connection entry when reference count reaches zero. */  \
    static inline void Prefix##_conn_free(Prefix##_ConnEntry *entry) {                         \
        if (!entry) return;                                                                    \
        Prefix##_conn_lock_acquire();                                                          \
                                                                                               \
        if (sqlite_atomic_load_32(&entry->refcount) > 0) {                                     \
            Prefix##_conn_lock_release();                                                      \
            return;                                                                            \
        }                                                                                      \
                                                                                               \
        if (Prefix##_conn_map && entry->db) {                                                  \
            sqlite3 *k = entry->db;                                                            \
            duo_hashmap_delete(Prefix##_conn_map, &k);                                         \
            if (duo_hashmap_count(Prefix##_conn_map) == 0) {                                  \
                duo_hashmap_free(Prefix##_conn_map);                                           \
                Prefix##_conn_map = NULL;                                                      \
            }                                                                                  \
            entry->db = NULL;                                                                  \
        }                                                                                      \
        Prefix##_conn_lock_release();                                                          \
                                                                                               \
        if (entry->free_fn) {                                                                  \
            entry->free_fn(&entry->state);                                                     \
        }                                                                                      \
        sqlite3_free(entry);                                                                   \
    }                                                                                          \
                                                                                               \
    /** @brief Decrements the reference counter and triggers teardown at zero. */              \
    static inline void Prefix##_conn_release(Prefix##_ConnEntry *entry) {                      \
        if (!entry) return;                                                                    \
        if (sqlite_atomic_decrement_32(&entry->refcount) == 0) {                               \
            Prefix##_conn_free(entry);                                                         \
        }                                                                                      \
    }                                                                                          \
                                                                                               \
    /** @brief SQLite bridge destructor callback for xDestroy. */                              \
    static inline void Prefix##_destructor(void *p) {                                          \
        Prefix##_ConnEntry *entry = (Prefix##_ConnEntry*)p;                                    \
        Prefix##_conn_release(entry);                                                          \
    }                                                                                          \
                                                                                               \
    /** @brief Retrieves the state pointer directly from a sqlite3* database handle. */        \
    static inline StateType* Prefix##_from_db(sqlite3 *db) {                                   \
        if (!db) return NULL;                                                                  \
        Prefix##_conn_lock_acquire();                                                          \
        Prefix##_ConnEntry **p_entry = Prefix##_conn_map ? (Prefix##_ConnEntry**)duo_hashmap_get(Prefix##_conn_map, &db) : NULL; \
        Prefix##_ConnEntry *entry = p_entry ? *p_entry : NULL;                                 \
        Prefix##_conn_lock_release();                                                          \
        return entry ? &entry->state : NULL;                                                   \
    }                                                                                          \
                                                                                               \
    /** @brief Explicitly unregisters and frees the state entry for a connection. */           \
    static inline void Prefix##_remove(sqlite3 *db) {                                          \
        if (!db) return;                                                                       \
        Prefix##_conn_lock_acquire();                                                          \
        Prefix##_ConnEntry **p_entry = Prefix##_conn_map ? (Prefix##_ConnEntry**)duo_hashmap_get(Prefix##_conn_map, &db) : NULL; \
        Prefix##_ConnEntry *entry = p_entry ? *p_entry : NULL;                                 \
        if (entry) {                                                                           \
            sqlite3 *k = db;                                                                   \
            duo_hashmap_delete(Prefix##_conn_map, &k);                                         \
            if (duo_hashmap_count(Prefix##_conn_map) == 0) {                                  \
                duo_hashmap_free(Prefix##_conn_map);                                           \
                Prefix##_conn_map = NULL;                                                      \
            }                                                                                  \
            entry->db = NULL;                                                                  \
            Prefix##_conn_lock_release();                                                      \
            Prefix##_conn_release(entry);                                                      \
            return;                                                                            \
        }                                                                                      \
        Prefix##_conn_lock_release();                                                          \
    }                                                                                          \
                                                                                               \
    /** @brief Allocates or retains state for a connection, returning a pointer for pApp. */   \
    static inline void* Prefix##_init(sqlite3 *db, void (*init_fn)(StateType*), void (*free_fn)(StateType*)) { \
        if (!db) return NULL;                                                                  \
        Prefix##_conn_lock_acquire();                                                          \
        Prefix##_ConnEntry **p_entry = Prefix##_conn_map ? (Prefix##_ConnEntry**)duo_hashmap_get(Prefix##_conn_map, &db) : NULL; \
        Prefix##_ConnEntry *entry = p_entry ? *p_entry : NULL;                                 \
        if (entry) {                                                                           \
            Prefix##_conn_retain(entry);                                                       \
            Prefix##_conn_lock_release();                                                      \
            return entry;                                                                      \
        }                                                                                      \
        Prefix##_conn_lock_release();                                                          \
                                                                                               \
        Prefix##_ConnEntry *new_entry = (Prefix##_ConnEntry*)sqlite3_malloc64(sizeof(Prefix##_ConnEntry)); \
        if (!new_entry) return NULL;                                                           \
        memset(new_entry, 0, sizeof(Prefix##_ConnEntry));                                      \
        new_entry->db = db;                                                                    \
        sqlite_atomic_store_32(&new_entry->refcount, 1);                                       \
        new_entry->free_fn = free_fn;                                                          \
        if (init_fn) init_fn(&new_entry->state);                                               \
                                                                                               \
        Prefix##_conn_lock_acquire();                                                          \
        p_entry = Prefix##_conn_map ? (Prefix##_ConnEntry**)duo_hashmap_get(Prefix##_conn_map, &db) : NULL; \
        entry = p_entry ? *p_entry : NULL;                                                     \
        if (entry) {                                                                           \
            Prefix##_conn_retain(entry);                                                       \
            Prefix##_conn_lock_release();                                                      \
            if (new_entry->free_fn) new_entry->free_fn(&new_entry->state);                     \
            sqlite3_free(new_entry);                                                           \
            return entry;                                                                      \
        }                                                                                      \
        if (!Prefix##_conn_map) {                                                              \
            Prefix##_conn_map = duo_hashmap_new(sizeof(sqlite3*), sizeof(Prefix##_ConnEntry*), 16, 0, 0, NULL, NULL, NULL, NULL, NULL); \
            if (!Prefix##_conn_map) {                                                          \
                Prefix##_conn_lock_release();                                                  \
                if (new_entry->free_fn) new_entry->free_fn(&new_entry->state);                 \
                sqlite3_free(new_entry);                                                       \
                return NULL;                                                                   \
            }                                                                                  \
        }                                                                                      \
        duo_hashmap_set(Prefix##_conn_map, &db, &new_entry);                                  \
        Prefix##_conn_lock_release();                                                          \
        return new_entry;                                                                      \
    }                                                                                          \
                                                                                               \
    /** @brief Fast resolution of connection state inside SQL functions. */                     \
    static inline StateType* Prefix##_from_context(sqlite3_context *ctx) {                     \
        if (!ctx) return NULL;                                                                 \
        Prefix##_ConnEntry *entry = (Prefix##_ConnEntry*)sqlite3_user_data(ctx);               \
        if (entry) return &entry->state;                                                       \
        sqlite3 *db = sqlite3_context_db_handle(ctx);                                          \
        return Prefix##_from_db(db);                                                           \
    }                                                                                          \
                                                                                               \
    /** @brief Fast resolution from raw entry pointer. */                                      \
    static inline StateType* Prefix##_from_ptr(void *p) {                                      \
        Prefix##_ConnEntry *entry = (Prefix##_ConnEntry*)p;                                    \
        return entry ? &entry->state : NULL;                                                   \
    }

/**
 * @brief Convenience macro generating per-connection state routines where Prefix matches StateType.
 */
#define SQLITE_CONNECTION_STATE(StateType) DEFINE_SQLITE_CONN_STATE(StateType, StateType)

/**
 * @brief Forward backward-compatibility include for unified hybrid state
 */
#include "sqlite3_hybrid_state.h"

#endif // SQLITE3_CONN_STATE_H
