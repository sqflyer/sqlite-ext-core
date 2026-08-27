#ifndef SQLITE3_EXT_HPP
#define SQLITE3_EXT_HPP

#include <sqlite3.h>

// Core subsystem headers
#include "sqlite3_allocator.hpp"
#include "sqlite3_smart_ptr.hpp"
#include "sqlite3_db.hpp"
#include "sqlite3_value.hpp"
#include "sqlite3_udf.hpp"
#include "sqlite3_aggregate.hpp"
#include "sqlite3_tvf.hpp"
#include "sqlite3_tvf_coro.hpp"
#include "sqlite3_vtab.hpp"
#include "sqlite3_ext_state.hpp"
#include "sqlite3_statement.hpp"
#include "sqlite3_transaction.hpp"
#include "sqlite3_buffer.hpp"
#include "sqlite3_blob_stream.hpp"
#include "sqlite3_time.hpp"

/**
 * @brief Unified, zero-overhead entry point for registering all SQLite extension components:
 *        - Scalar User-Defined Functions (UDFs)
 *        - Object-Oriented Aggregate Functions
 *        - Eponymous Table-Valued Functions (TVFs)
 *        - Full-featured C++ Virtual Tables (with transactions, savepoints, indexing)
 *        - Per-connection shared state management
 */
class SqliteExt {
public:
    template <typename T>
    using Allocator = SqliteAllocator<T>;
    // ========================================================================
    // 1. Shared State Management
    // ========================================================================

    /**
     * @brief Initializes or retrieves a singleton shared state struct bound to the database connection.
     * @tparam State The user-defined state struct type.
     * @tparam InitFunc Callable with signature `void(State*)` or `void(State&)`.
     * @param db The SQLite database connection (SqliteDatabaseView, SqliteDatabaseOwned, or sqlite3*).
     * @param init_fn Initializer callback executed exactly once upon state allocation.
     * @return Raw pointer to the shared State instance.
     */
    template <typename State, typename InitFunc>
    static inline State* init_state(SqliteDatabaseView db, InitFunc init_fn) {
        return SqliteExtState<State>::get_or_create(db, init_fn);
    }

    /**
     * @brief Initializes or retrieves a singleton shared state struct bound to the database connection.
     * @tparam State The user-defined state struct type.
     * @param db The SQLite database connection (SqliteDatabaseView, SqliteDatabaseOwned, or sqlite3*).
     * @return Raw pointer to the shared State instance.
     */
    template <typename State>
    static inline State* init_state(SqliteDatabaseView db) {
        return SqliteExtState<State>::get_or_create(db, nullptr);
    }

    /**
     * @brief Retrieves the existing shared state struct bound to the database connection.
     * @tparam State The user-defined state struct type.
     * @param db The SQLite database connection.
     * @return Pointer to State if initialized, or nullptr if not registered.
     */
    template <typename State>
    static inline State* get_state(SqliteDatabaseView db) {
        return SqliteExtState<State>::get(db);
    }

    // ========================================================================
    // 2. Scalar User-Defined Functions (UDFs)
    // ========================================================================

    /**
     * @brief Register a stateless C++ function pointer, lambda, or std::function as a Scalar UDF.
     * @param db The SQLite database connection.
     * @param name The SQL function name.
     * @param num_args Expected argument count (-1 for variadic).
     * @param func Callable scalar implementation `void(SqliteContext, SqliteUdfArgs)` or `void(sqlite3_context*, SqliteUdfArgs)`.
     * @param deterministic Whether the function is deterministic (default true).
     * @return SQLITE_OK on success, or an SQLite error code.
     */
    template <typename Func>
    static inline int define_scalar(SqliteDatabaseView db, const char* name, int num_args, Func func, bool deterministic = true) {
        return SqliteUdf::define(db, name, num_args, func, deterministic);
    }

    /**
     * @brief Register a stateless compile-time function template proxy as a Scalar UDF (Zero heap allocation).
     * @tparam Func The free function pointer `void(*)(SqliteContext, SqliteUdfArgs)`.
     * @param db The SQLite database connection.
     * @param name The SQL function name.
     * @param num_args Expected argument count (-1 for variadic).
     * @param deterministic Whether the function is deterministic (default true).
     * @return SQLITE_OK on success, or an SQLite error code.
     */
    template <SqliteUdf::ScalarFuncContext Func>
    static inline int define_scalar(SqliteDatabaseView db, const char* name, int num_args = -1, bool deterministic = true) {
        return SqliteUdf::define<Func>(db, name, num_args, deterministic);
    }

    template <SqliteUdf::ScalarFuncContext Func>
    static inline int define_scalar_proxy(SqliteDatabaseView db, const char* name, int num_args = -1, bool deterministic = true) {
        return SqliteUdf::define<Func>(db, name, num_args, deterministic);
    }

    /**
     * @brief Register a stateful compile-time function template proxy bound to connection shared state.
     * @tparam State The user-defined state struct type.
     * @tparam Func The free function pointer `void(*)(SqliteContext, SqliteUdfArgs)`.
     * @param db The SQLite database connection.
     * @param name The SQL function name.
     * @param num_args Expected argument count (-1 for variadic).
     * @param deterministic Whether the function is deterministic (default false).
     * @return SQLITE_OK on success, or an SQLite error code.
     */
    template <typename State, SqliteUdf::ScalarFuncContext Func>
    static inline int define_scalar_with_state(SqliteDatabaseView db, const char* name, int num_args = -1, bool deterministic = false) {
        return SqliteUdf::define_with_state<State, Func>(db, name, num_args, deterministic);
    }

    // ========================================================================
    // 3. Object-Oriented Aggregate Functions
    // ========================================================================

    /**
     * @brief Register an Object-Oriented C++ Aggregate Function (Stateless).
     * @tparam AggType The struct implementing `step(...)` and `finalize(...)`.
     * @param db The SQLite database connection.
     * @param name The SQL aggregate function name.
     * @param num_args Expected argument count (-1 for variadic).
     * @param deterministic Whether the aggregate is deterministic (default true).
     * @return SQLITE_OK on success, or an SQLite error code.
     */
    template <typename AggType>
    static inline int define_aggregate(SqliteDatabaseView db, const char* name, int num_args = -1, bool deterministic = true) {
        return SqliteAggregate::define<AggType>(db, name, num_args, deterministic);
    }

    /**
     * @brief Register an Object-Oriented C++ Aggregate Function bound to shared connection state.
     * @tparam State The user-defined state struct type.
     * @tparam AggType The struct implementing `step(...)` and `finalize(...)`.
     * @param db The SQLite database connection.
     * @param name The SQL aggregate function name.
     * @param num_args Expected argument count (-1 for variadic).
     * @param deterministic Whether the aggregate is deterministic (default false).
     * @return SQLITE_OK on success, or an SQLite error code.
     */
    template <typename State, typename AggType>
    static inline int define_aggregate_with_state(SqliteDatabaseView db, const char* name, int num_args = -1, bool deterministic = false) {
        return SqliteAggregate::define_with_state<State, AggType>(db, name, num_args, deterministic);
    }

    // ========================================================================
    // 4. Eponymous Table-Valued Functions (TVFs)
    // ========================================================================

    /**
     * @brief Register an Object-Oriented Table-Valued Function (TVF) (Stateless).
     * @tparam TvfType The iterator class inheriting from SqliteTvfIterator.
     * @param db The SQLite database connection.
     * @param name The SQL TVF name.
     * @return SQLITE_OK on success, or an SQLite error code.
     */
    template <typename TvfType>
    static inline int define_tvf(SqliteDatabaseView db, const char* name) {
        return SqliteTvf::define<TvfType>(db, name);
    }

    /**
     * @brief Register an Object-Oriented Table-Valued Function (TVF) bound to shared connection state.
     * @tparam State The user-defined state struct type.
     * @tparam TvfType The iterator class inheriting from SqliteTvfIterator.
     * @param db The SQLite database connection.
     * @param name The SQL TVF name.
     * @return SQLITE_OK on success, or an SQLite error code.
     */
    template <typename State, typename TvfType>
    static inline int define_tvf_with_state(SqliteDatabaseView db, const char* name) {
        return SqliteTvf::define_with_state<State, TvfType>(db, name);
    }

    /**
     * @brief Register a Coroutine-based Table-Valued Function (TVF) (Stateless).
     * @tparam TvfType The struct defining `schema()` and `generate(SqliteUdfArgs)`.
     * @param db The SQLite database connection.
     * @param name The SQL TVF name.
     * @return SQLITE_OK on success, or an SQLite error code.
     */
    template <typename TvfType>
    static inline int define_tvf_coro(SqliteDatabaseView db, const char* name) {
        return SqliteTvfCoro::define<TvfType>(db, name);
    }

    /**
     * @brief Register a Coroutine-based Table-Valued Function (TVF) bound to shared connection state.
     * @tparam State The user-defined state struct type.
     * @tparam TvfType The struct defining `schema()` and `generate(SqliteUdfArgs)`.
     * @param db The SQLite database connection.
     * @param name The SQL TVF name.
     * @return SQLITE_OK on success, or an SQLite error code.
     */
    template <typename State, typename TvfType>
    static inline int define_tvf_coro_with_state(SqliteDatabaseView db, const char* name) {
        return SqliteTvfCoro::define_with_state<State, TvfType>(db, name);
    }

    // ========================================================================
    // 5. Full C++ Virtual Tables
    // ========================================================================

    /**
     * @brief Register a C++ Virtual Table module with SQLite (Stateless).
     * @tparam VTableType The C++ class implementing the virtual table (inheriting from SqliteVTable).
     * @tparam Options Bitmask of VTabOptions (e.g. VTabOptions::Writable, VTabOptions::Eponymous).
     * @param db The SQLite database connection.
     * @param module_name The SQL virtual table module name.
     * @return SQLITE_OK on success, or an SQLite error code.
     */
    template <typename VTableType, VTabOptions Options = VTabOptions::ReadOnly>
    static inline int define_vtab(SqliteDatabaseView db, const char* module_name) {
        return SqliteVTab::define<VTableType, Options>(db, module_name);
    }

    /**
     * @brief Register a C++ Virtual Table module with SQLite bound to shared connection state.
     * @tparam State The user-defined state struct type.
     * @tparam VTableType The C++ class implementing the virtual table (inheriting from SqliteVTable).
     * @tparam Options Bitmask of VTabOptions (e.g. VTabOptions::Writable, VTabOptions::Eponymous).
     * @param db The SQLite database connection.
     * @param module_name The SQL virtual table module name.
     * @return SQLITE_OK on success, or an SQLite error code.
     */
    template <typename State, typename VTableType, VTabOptions Options = VTabOptions::ReadOnly>
    static inline int define_vtab_with_state(SqliteDatabaseView db, const char* module_name) {
        return SqliteVTab::define_with_state<State, VTableType, Options>(db, module_name);
    }

    // ========================================================================
    // 6. M:N Coroutine Scheduler & Worker Pool
    // ========================================================================

    /**
     * @brief Acquires the process-wide global coroutine scheduler with reference counting.
     * @param num_workers Background worker thread count (default 4; 0 for main-thread only).
     * @return Pointer to the shared global SqliteCoroScheduler.
     */
    static inline SqliteCoroScheduler* acquire_coro_scheduler(size_t num_workers = 4) {
        return SqliteCoroScheduler::acquire_global(num_workers);
    }

    /**
     * @brief Releases a reference to the process-wide global coroutine scheduler.
     */
    static inline void release_coro_scheduler() {
        SqliteCoroScheduler::release_global();
    }
};

#endif // SQLITE3_EXT_HPP
