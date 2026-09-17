/**
 * @file sqlite3_hybrid_state.h
 * @brief Unified hybrid state management for SQLite extensions (Pure C Macro API).
 * 
 * Pairs per-database shared state (SQLITE_EXTENSION_STATE) with per-connection
 * private state (SQLITE_CONNECTION_STATE) under a single SQLite pApp context
 * and dual-destructor teardown hook.
 */
#ifndef SQLITE3_HYBRID_STATE_H
#define SQLITE3_HYBRID_STATE_H

#include "sqlite3ext.h"
#include <sqlite3.h>
#include <string.h>
#include <stddef.h>

#include "sqlite3_ext_state.h"
#include "sqlite3_conn_state.h"

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
    /** @brief Internal carrier holding raw entry pointers for single xDestroy bridging. */           \
    typedef struct HybridPrefix##_HybridHolder {                                                      \
        ExtStateType *ext_raw;  /**< Pointer to ExtState Entry. */                                            \
        ConnStateType *conn_raw; /**< Pointer to ConnState Entry. */                                           \
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
        ExtStateType *ext_raw  = ExtPrefix##_init(db, ext_init, ext_free);                                    \
        ConnStateType *conn_raw = ConnPrefix##_init(db, conn_init, conn_free);                                 \
        HybridPrefix##_HybridHolder *holder =                                                         \
            (HybridPrefix##_HybridHolder*)sqlite3_malloc64(sizeof(HybridPrefix##_HybridHolder));      \
        if (holder) {                                                                                 \
            holder->ext_raw  = ext_raw;                                                               \
            holder->conn_raw = conn_raw;                                                              \
        }                                                                                             \
        return (void*)holder;                                                                         \
    }                                                                                                 \
                                                                                                      \
    /** @brief Resolves per-connection state from a sqlite3_context pointer (100% lock-free). */     \
    static inline ConnStateType* HybridPrefix##_hybrid_conn(sqlite3_context *ctx) {                   \
        if (!ctx) return NULL;                                                                        \
        HybridPrefix##_HybridHolder *holder = (HybridPrefix##_HybridHolder*)sqlite3_user_data(ctx);   \
        if (holder) {                                                                                 \
            return ConnPrefix##_from_ptr(holder->conn_raw);                                           \
        }                                                                                             \
        sqlite3 *db = sqlite3_context_db_handle(ctx);                                                 \
        return ConnPrefix##_from_db(db);                                                              \
    }                                                                                                 \
                                                                                                      \
    /** @brief Resolves per-connection state directly from a sqlite3* database handle. */             \
    static inline ConnStateType* HybridPrefix##_hybrid_conn_from_db(sqlite3 *db) {                    \
        if (!db) return NULL;                                                                         \
        return ConnPrefix##_from_db(db);                                                              \
    }                                                                                                 \
                                                                                                      \
    /** @brief Resolves shared per-database state from a sqlite3_context pointer. */                  \
    static inline ExtStateType* HybridPrefix##_hybrid_ext(sqlite3_context *ctx) {                     \
        if (!ctx) return NULL;                                                                        \
        HybridPrefix##_HybridHolder *holder = (HybridPrefix##_HybridHolder*)sqlite3_user_data(ctx);   \
        if (holder) {                                                                                 \
            ExtPrefix##_Entry *ext_entry = (ExtPrefix##_Entry*)holder->ext_raw;                       \
            return ext_entry ? &ext_entry->state : NULL;                                              \
        }                                                                                             \
        sqlite3 *db = sqlite3_context_db_handle(ctx);                                                 \
        return ExtPrefix##_from_db(ctx, db);                                                          \
    }                                                                                                 \
                                                                                                      \
    /** @brief Resolves shared per-database state directly from a sqlite3* database handle. */        \
    static inline ExtStateType* HybridPrefix##_hybrid_ext_from_db(sqlite3 *db) {                      \
        if (!db) return NULL;                                                                         \
        return ExtPrefix##_from_db_handle(db);                                                        \
    }                                                                                                 \
                                                                                                      \
    /** @brief Acquires write lock on the shared extension state component. */                        \
    static inline void HybridPrefix##_hybrid_write_acquire(ExtStateType *ext) {                       \
        if (ext) ExtPrefix##_write_acquire(ext);                                                      \
    }                                                                                                 \
                                                                                                      \
    /** @brief Releases write lock on the shared extension state component. */                        \
    static inline void HybridPrefix##_hybrid_write_release(ExtStateType *ext) {                       \
        if (ext) ExtPrefix##_write_release(ext);                                                      \
    }                                                                                                 \
                                                                                                      \
    /** @brief Acquires read lock on the shared extension state component. */                         \
    static inline void HybridPrefix##_hybrid_read_acquire(ExtStateType *ext) {                        \
        if (ext) ExtPrefix##_read_acquire(ext);                                                       \
    }                                                                                                 \
                                                                                                      \
    /** @brief Releases read lock on the shared extension state component. */                         \
    static inline void HybridPrefix##_hybrid_read_release(ExtStateType *ext) {                        \
        if (ext) ExtPrefix##_read_release(ext);                                                       \
    }

/**
 * @brief Convenience macro for generating a hybrid state registry when ExtPrefix and ConnPrefix match type names.
 */
#define SQLITE_HYBRID_STATE(ExtStateType, ConnStateType, HybridPrefix) \
    DEFINE_SQLITE_HYBRID_STATE(ExtStateType, ExtStateType, ConnStateType, ConnStateType, HybridPrefix)

#endif // SQLITE3_HYBRID_STATE_H
