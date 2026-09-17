/**
 * @file sqlite3_hybrid_state.hpp
 * @brief High-performance, thread-safe unified hybrid state manager for SQLite extensions (C++ Template API).
 * 
 * Packages both shared per-database state (`SqliteExtState`) and unique per-connection
 * state (`SqliteConnState`) into a single unified container with single-destructor bridging.
 */
#ifndef SQLITE3_HYBRID_STATE_HPP
#define SQLITE3_HYBRID_STATE_HPP

#include "sqlite3_hybrid_state.h"
#include "sqlite3_ext_state.hpp"
#include "sqlite3_conn_state.hpp"
#include "sqlite3_allocator.hpp"

#ifndef SQLITE_HYBRID_STATE_FWD_DECLARED
#define SQLITE_HYBRID_STATE_FWD_DECLARED
template <typename ExtT, typename ConnT, typename LockPolicy = SqliteRwLock>
class SqliteHybridState;
#endif

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
template <typename ExtT, typename ConnT, typename LockPolicy>
class SqliteHybridState {
public:
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
        if (!holder) {
            if (ext_raw) SqliteExtState<ExtT, LockPolicy>::destructor(ext_raw);
            if (conn_raw) SqliteConnState<ConnT>::destructor(conn_raw);
            return nullptr;
        }
        holder->ext_raw  = ext_raw;
        holder->conn_raw = conn_raw;
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
     * @brief Resolves per-connection private state from a sqlite3_context pointer.
     * 100% lock-free, zero touch on shared extension state (1 pointer dereference).
     */
    static ConnT* conn(sqlite3_context *ctx) {
        if (!ctx) return nullptr;

        Holder *holder = static_cast<Holder*>(sqlite3_user_data(ctx));
        if (holder) {
            return SqliteConnState<ConnT>::from_ptr(holder->conn_raw);
        }

        sqlite3 *db = sqlite3_context_db_handle(ctx);
        return SqliteConnState<ConnT>::from_db(db);
    }

    /**
     * @brief Resolves per-connection private state from a SqliteContext or context wrapper.
     */
    template <typename Ctx>
    static ConnT* conn(Ctx& ctx) {
        void *data = ctx.user_data();
        if (data) {
            Holder *holder = static_cast<Holder*>(data);
            return SqliteConnState<ConnT>::from_ptr(holder->conn_raw);
        }
        return conn(ctx.get());
    }

    /**
     * @brief Resolves per-connection private state directly from a sqlite3* database handle.
     */
    static ConnT* conn(sqlite3 *db) {
        if (!db) return nullptr;
        return SqliteConnState<ConnT>::get(db);
    }

    /**
     * @brief Resolves shared per-database state pointer from a sqlite3_context pointer.
     * Note: Access to shared state should be guarded via WriteGuard / ReadGuard.
     */
    static ExtT* ext(sqlite3_context *ctx) {
        if (!ctx) return nullptr;

        Holder *holder = static_cast<Holder*>(sqlite3_user_data(ctx));
        if (holder) {
            return SqliteExtState<ExtT, LockPolicy>::from_ptr(holder->ext_raw);
        }

        sqlite3 *db = sqlite3_context_db_handle(ctx);
        return SqliteExtState<ExtT, LockPolicy>::from_db(ctx, db);
    }

    /**
     * @brief Resolves shared per-database state pointer from a SqliteContext or context wrapper.
     */
    template <typename Ctx>
    static ExtT* ext(Ctx& ctx) {
        void *data = ctx.user_data();
        if (data) {
            Holder *holder = static_cast<Holder*>(data);
            return SqliteExtState<ExtT, LockPolicy>::from_ptr(holder->ext_raw);
        }
        return ext(ctx.get());
    }

    /**
     * @brief Resolves shared per-database state pointer directly from a sqlite3* database handle.
     */
    static ExtT* ext(sqlite3 *db) {
        if (!db) return nullptr;
        return SqliteExtState<ExtT, LockPolicy>::get(db);
    }

    /**
     * @brief Acquires write lock on the shared extension state component.
     */
    static void write_acquire(ExtT *e) {
        if (e) SqliteExtState<ExtT, LockPolicy>::write_acquire(e);
    }

    /**
     * @brief Releases write lock on the shared extension state component.
     */
    static void write_release(ExtT *e) {
        if (e) SqliteExtState<ExtT, LockPolicy>::write_release(e);
    }

    /**
     * @brief Acquires read lock on the shared extension state component.
     */
    static void read_acquire(ExtT *e) {
        if (e) SqliteExtState<ExtT, LockPolicy>::read_acquire(e);
    }

    /**
     * @brief Releases read lock on the shared extension state component.
     */
    static void read_release(ExtT *e) {
        if (e) SqliteExtState<ExtT, LockPolicy>::read_release(e);
    }

    /**
     * @brief RAII guard for acquiring and automatically releasing an EXCLUSIVE (write) lock on shared ext state.
     */
    class WriteGuard {
    private:
        typename SqliteExtState<ExtT, LockPolicy>::WriteGuard guard_;
    public:
        explicit WriteGuard(ExtT *e) : guard_(e) {}
        explicit WriteGuard(sqlite3_context *ctx) : guard_(ext(ctx)) {}
        template <typename Ctx>
        explicit WriteGuard(Ctx &ctx) : guard_(ext(ctx)) {}
        explicit WriteGuard(sqlite3 *db) : guard_(ext(db)) {}

        ExtT* get() noexcept { return guard_.get(); }
        ExtT* operator->() noexcept { return guard_.operator->(); }
        ExtT& operator*() noexcept { return guard_.operator*(); }
        explicit operator bool() const noexcept { return guard_.get() != nullptr; }
    };

    /**
     * @brief RAII guard for acquiring and automatically releasing a SHARED (read) lock on shared ext state.
     */
    class ReadGuard {
    private:
        typename SqliteExtState<ExtT, LockPolicy>::ReadGuard guard_;
    public:
        explicit ReadGuard(ExtT *e) : guard_(e) {}
        explicit ReadGuard(sqlite3_context *ctx) : guard_(ext(ctx)) {}
        template <typename Ctx>
        explicit ReadGuard(Ctx &ctx) : guard_(ext(ctx)) {}
        explicit ReadGuard(sqlite3 *db) : guard_(ext(db)) {}

        ExtT* get() noexcept { return guard_.get(); }
        ExtT* operator->() noexcept { return guard_.operator->(); }
        ExtT& operator*() noexcept { return guard_.operator*(); }
        explicit operator bool() const noexcept { return guard_.get() != nullptr; }
    };
};

template <typename ExtT, typename ConnT, typename LockPolicy = SqliteRwLock>
using SqliteHybrid = SqliteHybridState<ExtT, ConnT, LockPolicy>;

#endif // SQLITE3_HYBRID_STATE_HPP
