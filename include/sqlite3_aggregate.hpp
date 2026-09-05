#ifndef SQLITE3_AGGREGATE_HPP
#define SQLITE3_AGGREGATE_HPP

#include <sqlite3.h>
#include "sqlite3_db.hpp"
#include "sqlite3_value.hpp"
#include "sqlite3_allocator.hpp"
#include "sqlite3_ext_state.hpp"
#include "sqlite3_buffer.hpp"


/**
 * @brief Base marker tag struct for compile-time inheritance verification.
 */
struct SqliteAggregateMarker {};

/**
 * @brief Templated base class for SQLite Aggregate Functions with typed return values.
 * 
 * User aggregate structs must publicly inherit from `SqliteAggregateBase<ReturnType>`
 * (or `SqliteAggregateBase<void>` for context-aware finalizers) and implement `step()` and `finalize()`.
 * 
 * @tparam ReturnType The C++ type returned by `finalize()`. Defaults to `void`.
 */
template <typename ReturnType = void>
class SqliteAggregateBase : public SqliteAggregateMarker {
public:
    /**
     * @brief Virtual destructor to ensure proper cleanup of derived aggregate instances
     * when destroyed polymorphically via base pointer in sqlite_delete().
     */
    virtual ~SqliteAggregateBase() = default;
    /**
     * @brief Aggregation step callback invoked for each row.
     * @param args Bounds-safe wrapper over row argument values.
     */
    virtual void step(SqliteUdfArgs args) {
        (void)args;
    }

    /**
     * @brief Aggregation step callback with access to SqliteContext.
     * @param ctx The SQLite context wrapper.
     * @param args Bounds-safe wrapper over row argument values.
     */
    virtual void step(SqliteContext ctx, SqliteUdfArgs args) {
        (void)ctx;
        step(args);
    }

    /**
     * @brief Aggregation finalize callback invoked once to produce the result.
     * @return The strongly-typed aggregation result.
     */
    virtual ReturnType finalize() {
        return ReturnType();
    }

    /**
     * @brief Aggregation finalize callback with access to SqliteContext.
     * @param ctx The SQLite context wrapper.
     * @return The strongly-typed aggregation result.
     */
    virtual ReturnType finalize(SqliteContext ctx) {
        (void)ctx;
        return finalize();
    }
};

/**
 * @brief Template specialization for void-returning (context-aware) aggregate functions.
 */
template <>
class SqliteAggregateBase<void> : public SqliteAggregateMarker {
public:
    /**
     * @brief Virtual destructor to ensure proper cleanup of derived aggregate instances
     * when destroyed polymorphically via base pointer in sqlite_delete().
     */
    virtual ~SqliteAggregateBase() = default;
    /**
     * @brief Aggregation step callback invoked for each row.
     * @param args Bounds-safe wrapper over row argument values.
     */
    virtual void step(SqliteUdfArgs args) {
        (void)args;
    }

    /**
     * @brief Aggregation step callback with access to SqliteContext.
     * @param ctx The SQLite context wrapper.
     * @param args Bounds-safe wrapper over row argument values.
     */
    virtual void step(SqliteContext ctx, SqliteUdfArgs args) {
        (void)ctx;
        step(args);
    }

    /**
     * @brief Aggregation finalize callback with direct access to SqliteContext.
     * @param ctx The SQLite context wrapper to write results or errors to.
     */
    virtual void finalize(SqliteContext ctx) {
        (void)ctx;
    }
};

namespace SqliteAggregateDetail {

    /**
     * @brief Zero-dependency C++11 compile-time inheritance verification trait.
     * Evaluates to true if Derived inherits from Base.
     */
    template <typename Base, typename Derived>
    struct is_base_of {
    private:
        typedef char yes[1];
        typedef char no[2];

        static yes& test(Base*);
        static no&  test(...);

    public:
        static const bool value = sizeof(test(static_cast<Derived*>(nullptr))) == sizeof(yes);
    };

    /**
     * @brief Overloaded helpers to write return values directly to the SQLite execution context.
     */
    inline void set_sqlite_result(sqlite3_context* ctx, int val) { sqlite3_result_int(ctx, val); }
    inline void set_sqlite_result(sqlite3_context* ctx, sqlite3_int64 val) { sqlite3_result_int64(ctx, val); }
    inline void set_sqlite_result(sqlite3_context* ctx, double val) { sqlite3_result_double(ctx, val); }
    inline void set_sqlite_result(sqlite3_context* ctx, bool val) { sqlite3_result_int(ctx, val ? 1 : 0); }
    inline void set_sqlite_result(sqlite3_context* ctx, decltype(nullptr)) { sqlite3_result_null(ctx); }
    inline void set_sqlite_result(sqlite3_context* ctx, const char* val) {
        if (val) {
            sqlite3_result_text(ctx, val, -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_result_null(ctx);
        }
    }
    inline void set_sqlite_result(sqlite3_context* ctx, const SqliteStringView& val) { val.result(ctx); }
    inline void set_sqlite_result(sqlite3_context* ctx, const SqliteStringOwned& val) { val.result(ctx); }
    inline void set_sqlite_result(sqlite3_context* ctx, const SqliteBlobView& val) { val.result(ctx); }
    inline void set_sqlite_result(sqlite3_context* ctx, const SqliteBlobOwned& val) { val.result(ctx); }
    inline void set_sqlite_result(sqlite3_context* ctx, const SqliteValueView& val) { val.result(ctx); }
    inline void set_sqlite_result(sqlite3_context* ctx, const SqliteValueOwned& val) { val.result(ctx); }
    inline void set_sqlite_result(sqlite3_context* ctx, const SqliteBuffer& val) { val.result(ctx); }
    inline void set_sqlite_result(sqlite3_context* ctx, const SqliteString& val) { val.result(ctx); }
    inline void set_sqlite_result(sqlite3_context* ctx, const SqliteBufferSlice& val) { val.result(ctx); }

    /**
     * @brief Tag dispatch priority hierarchy for unambiguous SFINAE method resolution.
     */
    struct PriorityRank3 {};
    struct PriorityRank2 : PriorityRank3 {};
    struct PriorityRank1 : PriorityRank2 {};
    struct PriorityRank0 : PriorityRank1 {};

    /**
     * @brief SFINAE helper to invoke `agg->step(SqliteContext(ctx), args)` or `agg->step(args)`.
     */
    template <typename U>
    inline auto invoke_step_impl(U* agg, sqlite3_context* ctx, SqliteUdfArgs args, PriorityRank0)
        -> decltype(agg->step(SqliteContext(ctx), args), void()) {
        agg->step(SqliteContext(ctx), args);
    }

    template <typename U>
    inline auto invoke_step_impl(U* agg, sqlite3_context*, SqliteUdfArgs args, PriorityRank1)
        -> decltype(agg->step(args), void()) {
        agg->step(args);
    }

    template <typename U>
    inline void invoke_step(U* agg, sqlite3_context* ctx, SqliteUdfArgs args) {
        invoke_step_impl(agg, ctx, args, PriorityRank0{});
    }

    /**
     * @brief SFINAE helper to invoke typed finalize(ctx), void finalize(ctx), typed finalize(), or void finalize().
     */
    template <typename U>
    inline auto invoke_finalize_impl(U* agg, sqlite3_context* ctx, PriorityRank0)
        -> decltype(set_sqlite_result(ctx, agg->finalize(SqliteContext(ctx))), void()) {
        set_sqlite_result(ctx, agg->finalize(SqliteContext(ctx)));
    }

    template <typename U>
    inline auto invoke_finalize_impl(U* agg, sqlite3_context* ctx, PriorityRank1)
        -> decltype(agg->finalize(SqliteContext(ctx)), void()) {
        agg->finalize(SqliteContext(ctx));
    }

    template <typename U>
    inline auto invoke_finalize_impl(U* agg, sqlite3_context* ctx, PriorityRank2)
        -> decltype(set_sqlite_result(ctx, agg->finalize()), void()) {
        set_sqlite_result(ctx, agg->finalize());
    }

    template <typename U>
    inline auto invoke_finalize_impl(U* agg, sqlite3_context*, PriorityRank3)
        -> decltype(agg->finalize(), void()) {
        agg->finalize();
    }

    template <typename U>
    inline void invoke_finalize(U* agg, sqlite3_context* ctx) {
        invoke_finalize_impl(agg, ctx, PriorityRank0{});
    }

#if defined(_MSC_VER)
#pragma warning(push)
// MSVC /W4 emits warning C4324 ('structure was padded due to alignment specifier')
// when padding bytes are inserted between 'bool initialized' (1 byte) and 'storage'
// to satisfy alignas(T). This padding is intentional and strictly required to ensure
// aligned placement-new construction of user aggregate types in SQLite memory arenas.
#pragma warning(disable: 4324)
#endif

    /**
     * @brief Internal holder stored inside sqlite3_aggregate_context.
     * 
     * Encapsulates initialization state and raw aligned memory storage for placement-new
     * instantiation of user aggregate classes.
     */
    template <typename T>
    struct AggregateHolder {
        bool initialized;
        alignas(T) unsigned char storage[sizeof(T)];

        inline T* instance() {
            return reinterpret_cast<T*>(storage);
        }
    };

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}

/**
 * @brief Zero-overhead C++ template framework for defining SQLite Aggregate Functions.
 * 
 * Manages aggregate state lifetimes via `sqlite3_aggregate_context`, automatic
 * placement construction, step dispatching, and destructor finalization without exceptions or RTTI.
 * 
 * @tparam T The user's aggregate struct or class, which must inherit from `SqliteAggregateBase`.
 */
template <typename T>
class SqliteAggregateModule {
    static_assert(SqliteAggregateDetail::is_base_of<SqliteAggregateMarker, T>::value,
                  "Custom aggregate struct must publicly inherit from SqliteAggregateBase<ReturnType>!");

public:
    /**
     * @brief Register a C++ struct as an Aggregate Function with SQLite.
     * 
     * @param db The SQLite database connection (SqliteDatabaseView, SqliteDatabaseOwned, or sqlite3*).
     * @param name The SQL function name.
     * @param num_args The expected number of arguments (-1 for variadic).
     * @param deterministic Whether the function is deterministic (default true).
     * @return SQLITE_OK on success, or an error code.
     */
    static int define(SqliteDatabaseView db, const char* name, int num_args = -1, bool deterministic = true) {
        int flags = SQLITE_UTF8 | SQLITE_SUBTYPE | (deterministic ? SQLITE_DETERMINISTIC : 0);

        return sqlite3_create_function_v2(
            db.get(),
            name,
            num_args,
            flags,
            nullptr, // pApp
            nullptr, // xFunc
            &SqliteAggregateModule<T>::step_proxy,
            &SqliteAggregateModule<T>::final_proxy,
            nullptr  // xDestroy
        );
    }

    /**
     * @brief Register a C++ struct as a Stateful Aggregate Function with SQLite,
     * binding a shared state type (SqliteExtState<State>) to SQLite's user_data (pApp)
     * with automated xDestroy garbage collection.
     * 
     * @tparam State The state struct type managed by SqliteExtState<State>.
     * @param db The SQLite database connection (SqliteDatabaseView, SqliteDatabaseOwned, or sqlite3*).
     * @param name The SQL function name.
     * @param num_args The expected number of arguments (-1 for variadic).
     * @param deterministic Whether the function is deterministic (default false).
     * @return SQLITE_OK on success, or an error code.
     */
    template <typename State>
    static int define_with_state(SqliteDatabaseView db, const char* name, int num_args = -1, bool deterministic = false) {
        void* raw_state = SqliteExtState<State>::init(db.get());
        int flags = SQLITE_UTF8 | SQLITE_SUBTYPE | (deterministic ? SQLITE_DETERMINISTIC : 0);

        return sqlite3_create_function_v2(
            db.get(),
            name,
            num_args,
            flags,
            raw_state,
            nullptr,   // xFunc
            &SqliteAggregateModule<T>::step_proxy,
            &SqliteAggregateModule<T>::final_proxy,
            SqliteExtState<State>::destructor // Automatically garbage-collected by SQLite!
        );
    }

private:
    /**
     * @brief The internal xStep C callback executed by SQLite for each row.
     */
    static void step_proxy(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
        using Holder = SqliteAggregateDetail::AggregateHolder<T>;
        
        Holder* holder = static_cast<Holder*>(sqlite3_aggregate_context(ctx, sizeof(Holder)));
        if (!holder) {
            sqlite3_result_error_nomem(ctx);
            return;
        }

        // SQLite zeroes the allocated memory on first invocation
        if (!holder->initialized) {
            sqlite_construct_at(holder->instance());
            holder->initialized = true;
        }

        SqliteUdfArgs args(argc, argv);
        SqliteAggregateDetail::invoke_step(holder->instance(), ctx, args);
    }

    /**
     * @brief The internal xFinal C callback executed by SQLite to produce the aggregate result.
     */
    static void final_proxy(sqlite3_context* ctx) {
        using Holder = SqliteAggregateDetail::AggregateHolder<T>;
        
        Holder* holder = static_cast<Holder*>(sqlite3_aggregate_context(ctx, 0));
        
        if (holder && holder->initialized) {
            SqliteAggregateDetail::invoke_finalize(holder->instance(), ctx);
            holder->instance()->T::~T();
            holder->initialized = false;
        } else {
            // No rows stepped: evaluate finalize on a default-constructed instance
            T empty_agg;
            SqliteAggregateDetail::invoke_finalize(&empty_agg, ctx);
        }
    }
};

/**
 * @brief High-level helper class for Aggregate Function registration.
 */
class SqliteAggregate {
public:
    /**
     * @brief Register an Object-Oriented C++ Aggregate Function (Stateless).
     * @tparam T The aggregate struct/class implementing step() and finalize().
     * @param db The SQLite database connection (SqliteDatabaseView, SqliteDatabaseOwned, or sqlite3*).
     * @param name The SQL function name.
     * @param num_args Expected argument count (-1 for variadic).
     * @param deterministic Whether the aggregate is deterministic (default true).
     * @return SQLITE_OK on success, or an error code.
     */
    template <typename T>
    static inline int define(SqliteDatabaseView db, const char* name, int num_args = -1, bool deterministic = true) {
        return SqliteAggregateModule<T>::define(db, name, num_args, deterministic);
    }

    /**
     * @brief Register an Object-Oriented C++ Aggregate Function bound to shared connection state.
     * @tparam State The user-defined state struct type.
     * @tparam T The aggregate struct/class implementing step() and finalize().
     * @param db The SQLite database connection (SqliteDatabaseView, SqliteDatabaseOwned, or sqlite3*).
     * @param name The SQL function name.
     * @param num_args Expected argument count (-1 for variadic).
     * @param deterministic Whether the aggregate is deterministic (default false).
     * @return SQLITE_OK on success, or an error code.
     */
    template <typename State, typename T>
    static inline int define_with_state(SqliteDatabaseView db, const char* name, int num_args = -1, bool deterministic = false) {
        return SqliteAggregateModule<T>::template define_with_state<State>(db, name, num_args, deterministic);
    }
};

#endif // SQLITE3_AGGREGATE_HPP
