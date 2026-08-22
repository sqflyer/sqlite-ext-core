#ifndef SQLITE3_AGGREGATE_HPP
#define SQLITE3_AGGREGATE_HPP

#include <sqlite3.h>
#include "sqlite3_value.hpp"
#include "sqlite3_allocator.hpp"

/**
 * @brief Forward declaration of SqliteUdfArgs.
 */
class SqliteUdfArgs;

#ifndef SQLITE3_UDF_ARGS_DEFINED
#define SQLITE3_UDF_ARGS_DEFINED
/**
 * @brief Bounds-safe C++ wrapper over SQLite's raw (int argc, sqlite3_value** argv)
 */
class SqliteUdfArgs {
private:
    int m_argc;
    sqlite3_value** m_argv;

public:
    inline SqliteUdfArgs(int argc, sqlite3_value** argv) : m_argc(argc), m_argv(argv) {}

    /**
     * @brief Get the number of arguments passed to the UDF / Aggregate step.
     */
    inline int size() const { return m_argc; }

    /**
     * @brief Safely access an argument as a zero-allocation SqliteValueView.
     * 
     * If the index is out of bounds, returns a SQLITE_NULL view to prevent segfaults.
     */
    inline SqliteValueView operator[](int index) const {
        if (index < 0 || index >= m_argc) {
            return SqliteValueView(nullptr);
        }
        return SqliteValueView(m_argv[index]);
    }
};
#endif // SQLITE3_UDF_ARGS_DEFINED

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
protected:
    ~SqliteAggregateBase() = default;

public:

    /**
     * @brief Aggregation step callback invoked for each row.
     * @param args Bounds-safe wrapper over row argument values.
     */
    virtual void step(SqliteUdfArgs args) {
        (void)args;
    }

    /**
     * @brief Aggregation step callback with access to sqlite3_context.
     * @param ctx The SQLite context handle.
     * @param args Bounds-safe wrapper over row argument values.
     */
    virtual void step(sqlite3_context* ctx, SqliteUdfArgs args) {
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
};

/**
 * @brief Template specialization for void-returning (context-aware) aggregate functions.
 */
template <>
class SqliteAggregateBase<void> : public SqliteAggregateMarker {
protected:
    ~SqliteAggregateBase() = default;

public:

    /**
     * @brief Aggregation step callback invoked for each row.
     * @param args Bounds-safe wrapper over row argument values.
     */
    virtual void step(SqliteUdfArgs args) {
        (void)args;
    }

    /**
     * @brief Aggregation step callback with access to sqlite3_context.
     * @param ctx The SQLite context handle.
     * @param args Bounds-safe wrapper over row argument values.
     */
    virtual void step(sqlite3_context* ctx, SqliteUdfArgs args) {
        (void)ctx;
        step(args);
    }

    /**
     * @brief Aggregation finalize callback with direct access to sqlite3_context.
     * @param ctx The SQLite context handle to write results or errors to.
     */
    virtual void finalize(sqlite3_context* ctx) {
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
        // We use arrays of different sizes to differentiate between function overloads at compile time.
        // sizeof(yes) == 1, sizeof(no) == 2.
        typedef char yes[1];
        typedef char no[2];

        // Overload 1: Selected if Derived* can be implicitly cast to Base*.
        // This only happens if Derived inherits from Base.
        static yes& test(Base*);

        // Overload 2: Selected if the cast fails.
        // Varargs (...) have the lowest possible priority in C++ overload resolution.
        static no&  test(...);

    public:
        // We pretend to call test() with a Derived*. The sizeof() operator evaluates 
        // the return type's size without executing any code. If the inheritance is valid,
        // it chooses test(Base*) returning `yes`. Otherwise, it falls back to test(...) returning `no`.
        static const bool value = sizeof(test(static_cast<Derived*>(nullptr))) == sizeof(yes);
    };

    /**
     * @brief Overloaded helpers to write return values directly to the SQLite execution context.
     * 
     * When your Aggregate struct's `finalize()` method returns a value (like `double`, `int`, 
     * or a C++ wrapper like `SqliteStringOwned`), these overloads automatically route it 
     * to the correct underlying `sqlite3_result_...` C-API function without any runtime overhead.
     * 
     * BEST PRACTICE: For strings and blobs, it is highly recommended to return `SqliteStringOwned` 
     * or `SqliteBlobOwned` from your `finalize()` method rather than raw pointers. The SFINAE 
     * dispatcher will automatically call their `.result(ctx)` method, which safely manages 
     * memory ownership and lifetime with SQLite.
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

    /**
     * @brief Tag dispatch priority hierarchy for unambiguous SFINAE method resolution.
     *
     * When SFINAE (Substitution Failure Is Not An Error) finds multiple matching templates,
     * the compiler will throw an "ambiguous call" error unless one is strictly preferred.
     * By passing PriorityRank0, the compiler will try PriorityRank0 first. If substitution
     * fails (e.g. the method signature doesn't exist), it automatically falls back up the 
     * inheritance chain to PriorityRank1, and then PriorityRank2, eliminating ambiguity.
     *
     * WHY THIS IS NEEDED:
     * Because `SqliteAggregateBase` provides a default `virtual void step(SqliteUdfArgs args)`, 
     * every derived aggregate struct automatically inherits it. If a user writes a custom
     * context-aware `step(sqlite3_context*, SqliteUdfArgs)`, their struct now secretly has BOTH methods.
     * 
     * When SFINAE evaluates `decltype(agg->step(ctx, args))` and `decltype(agg->step(args))`,
     * BOTH are technically valid, so the compiler doesn't know which one to pick and throws
     * an "ambiguous call to overloaded function" error.
     * 
     * THE SOLUTION:
     * We pass a dummy `PriorityRank` object to force the compiler to check them in a strict order.
     * The compiler tries to match the arguments exactly first (`PriorityRank0`). 
     * - If `step(ctx, args)` is valid, it picks the `PriorityRank0` overload immediately.
     * - If it's invalid, it falls back to the `PriorityRank1` overload because `PriorityRank0`
     *   inherits from `PriorityRank1` and can be implicitly cast to it.
     * This dummy inheritance tree completely eliminates the ambiguity!
     */
    struct PriorityRank2 {};                                // Lowest priority fallback
    struct PriorityRank1 : PriorityRank2 {};                // Medium priority fallback
    struct PriorityRank0 : PriorityRank1 {};                // Highest priority (tried first)

    /**
     * @brief SFINAE helper to invoke `agg->step(ctx, args)` or `agg->step(args)`.
     */
    template <typename U>
    inline auto invoke_step_impl(U* agg, sqlite3_context* ctx, SqliteUdfArgs args, PriorityRank0)
        -> decltype(agg->step(ctx, args), void()) {
        agg->step(ctx, args);
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
     * @brief SFINAE helper to invoke `agg->finalize(ctx)`, `set_sqlite_result(ctx, agg->finalize())`, or `agg->finalize()`.
     */
    template <typename U>
    inline auto invoke_finalize_impl(U* agg, sqlite3_context* ctx, PriorityRank0)
        -> decltype(agg->finalize(ctx), void()) {
        agg->finalize(ctx);
    }

    template <typename U>
    inline auto invoke_finalize_impl(U* agg, sqlite3_context* ctx, PriorityRank1)
        -> decltype(set_sqlite_result(ctx, agg->finalize()), void()) {
        set_sqlite_result(ctx, agg->finalize());
    }

    template <typename U>
    inline auto invoke_finalize_impl(U* agg, sqlite3_context*, PriorityRank2)
        -> decltype(agg->finalize(), void()) {
        agg->finalize();
    }

    template <typename U>
    inline void invoke_finalize(U* agg, sqlite3_context* ctx) {
        invoke_finalize_impl(agg, ctx, PriorityRank0{});
    }

    /**
     * @brief Internal holder stored inside sqlite3_aggregate_context.
     */
    template <typename T>
    struct AggregateHolder {
        bool initialized;
        alignas(T) unsigned char storage[sizeof(T)];

        inline T* instance() {
            return reinterpret_cast<T*>(storage);
        }
    };
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
class SqliteAggregate {
    static_assert(SqliteAggregateDetail::is_base_of<SqliteAggregateMarker, T>::value,
                  "Custom aggregate struct must publicly inherit from SqliteAggregateBase<ReturnType>!");

public:
    /**
     * @brief Register a C++ struct as an Aggregate Function with SQLite.
     * 
     * @param db The SQLite database connection handle.
     * @param name The SQL function name.
     * @param num_args The expected number of arguments (-1 for variadic).
     * @param deterministic Whether the function is deterministic (default true).
     * @return SQLITE_OK on success, or an error code.
     */
    static int define(sqlite3* db, const char* name, int num_args = -1, bool deterministic = true) {
        int flags = SQLITE_UTF8;
        if (deterministic) {
            flags |= SQLITE_DETERMINISTIC;
        }

        return sqlite3_create_function_v2(
            db,
            name,
            num_args,
            flags,
            nullptr,
            nullptr,
            &SqliteAggregate<T>::step_proxy,
            &SqliteAggregate<T>::final_proxy,
            nullptr
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
            holder->instance()->~T();
            holder->initialized = false;
        } else {
            // No rows stepped: evaluate finalize on a default-constructed instance
            T empty_agg;
            SqliteAggregateDetail::invoke_finalize(&empty_agg, ctx);
        }
    }
};

#endif // SQLITE3_AGGREGATE_HPP
