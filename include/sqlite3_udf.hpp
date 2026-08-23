#ifndef SQLITE3_UDF_HPP
#define SQLITE3_UDF_HPP

#include <sqlite3.h>
#include "sqlite3_db.hpp"
#include "sqlite3_value.hpp"
#include "sqlite3_ext_state.hpp"

/**
 * @brief Lightweight registry for mapping C++ stateless functions/lambdas, stateful extension functions, and aggregates to SQLite UDFs.
 */
class SqliteUdf {
public:
    /**
     * @brief The C++ scalar function signatures.
     * 
     * BEST PRACTICE FOR RESULTS:
     * When writing scalar functions, you must explicitly set the result on the `ctx` before returning.
     * While you can use the raw C-APIs (`sqlite3_result_int`, `sqlite3_result_text`), it is highly 
     * recommended to use the wrapper types' `.result(ctx)` method when dealing with dynamic memory 
     * (Strings and Blobs) or `SqliteContext` methods. This prevents memory leaks and simplifies ownership.
     * 
     * Example:
     *   SqliteStringOwned str(ctx);
     *   str.appendall("hello");
     *   str.result(ctx); // Automatically calls sqlite3_result_text and safely transfers memory ownership!
     */
    typedef void (*ScalarFuncRaw)(sqlite3_context* ctx, SqliteUdfArgs args);
    typedef void (*ScalarFuncContext)(SqliteContext ctx, SqliteUdfArgs args);
    typedef void (*ScalarFuncContextRef)(SqliteContext& ctx, SqliteUdfArgs args);
    typedef ScalarFuncRaw ScalarFunc; // Backward compatibility

    // ========================================================================
    // STATELESS UDF REGISTRATION
    // ========================================================================

    /**
     * @brief Register a C++ scalar function with SQLite accepting raw sqlite3_context*.
     * 
     * @param db The SQLite database connection (SqliteDatabaseView, SqliteDatabaseOwned, or sqlite3*).
     * @param name The SQL name of the function.
     * @param num_args Expected argument count (-1 for variadic).
     * @param func A C++ function pointer or stateless lambda matching ScalarFuncRaw.
     * @param deterministic Whether the function is deterministic (default true).
     * @return SQLITE_OK on success, or an error code.
     */
    static int define(SqliteDatabaseView db, const char* name, int num_args, ScalarFuncRaw func, bool deterministic = true) {
        int flags = SQLITE_UTF8 | (deterministic ? SQLITE_DETERMINISTIC : 0);
        return sqlite3_create_function_v2(
            db.get(),
            name,
            num_args,
            flags,
            reinterpret_cast<void*>(func),
            &SqliteUdf::scalar_proxy_raw,
            nullptr, // xStep
            nullptr, // xFinal
            nullptr  // xDestroy
        );
    }

    /**
     * @brief Register a C++ scalar function with SQLite accepting modern SqliteContext (by value).
     * 
     * @param db The SQLite database connection (SqliteDatabaseView, SqliteDatabaseOwned, or sqlite3*).
     * @param name The SQL name of the function.
     * @param num_args Expected argument count (-1 for variadic).
     * @param func A C++ function pointer or stateless lambda matching ScalarFuncContext.
     * @param deterministic Whether the function is deterministic (default true).
     * @return SQLITE_OK on success, or an error code.
     */
    static int define(SqliteDatabaseView db, const char* name, int num_args, ScalarFuncContext func, bool deterministic = true) {
        int flags = SQLITE_UTF8 | (deterministic ? SQLITE_DETERMINISTIC : 0);
        return sqlite3_create_function_v2(
            db.get(),
            name,
            num_args,
            flags,
            reinterpret_cast<void*>(func),
            &SqliteUdf::scalar_proxy_context,
            nullptr, // xStep
            nullptr, // xFinal
            nullptr  // xDestroy
        );
    }

    /**
     * @brief Register a C++ scalar function with SQLite accepting modern SqliteContext& (by reference).
     * 
     * @param db The SQLite database connection (SqliteDatabaseView, SqliteDatabaseOwned, or sqlite3*).
     * @param name The SQL name of the function.
     * @param num_args Expected argument count (-1 for variadic).
     * @param func A C++ function pointer or stateless lambda matching ScalarFuncContextRef.
     * @param deterministic Whether the function is deterministic (default true).
     * @return SQLITE_OK on success, or an error code.
     */
    static int define(SqliteDatabaseView db, const char* name, int num_args, ScalarFuncContextRef func, bool deterministic = true) {
        int flags = SQLITE_UTF8 | (deterministic ? SQLITE_DETERMINISTIC : 0);
        return sqlite3_create_function_v2(
            db.get(),
            name,
            num_args,
            flags,
            reinterpret_cast<void*>(func),
            &SqliteUdf::scalar_proxy_context_ref,
            nullptr, // xStep
            nullptr, // xFinal
            nullptr  // xDestroy
        );
    }

    /**
     * @brief Register a stateless C++ scalar function as a zero-allocation compile-time template proxy.
     */
    template <void (*Func)(SqliteContext, SqliteUdfArgs)>
    static int define(SqliteDatabaseView db, const char* name, int num_args = -1, bool deterministic = true) {
        int flags = SQLITE_UTF8 | (deterministic ? SQLITE_DETERMINISTIC : 0);
        return sqlite3_create_function_v2(
            db.get(),
            name,
            num_args,
            flags,
            nullptr,
            &SqliteUdf::template_proxy_context<Func>,
            nullptr,
            nullptr,
            nullptr
        );
    }

    template <void (*Func)(SqliteContext&, SqliteUdfArgs)>
    static int define(SqliteDatabaseView db, const char* name, int num_args = -1, bool deterministic = true) {
        int flags = SQLITE_UTF8 | (deterministic ? SQLITE_DETERMINISTIC : 0);
        return sqlite3_create_function_v2(
            db.get(),
            name,
            num_args,
            flags,
            nullptr,
            &SqliteUdf::template_proxy_context_ref<Func>,
            nullptr,
            nullptr,
            nullptr
        );
    }

    template <void (*Func)(sqlite3_context*, SqliteUdfArgs)>
    static int define(SqliteDatabaseView db, const char* name, int num_args = -1, bool deterministic = true) {
        int flags = SQLITE_UTF8 | (deterministic ? SQLITE_DETERMINISTIC : 0);
        return sqlite3_create_function_v2(
            db.get(),
            name,
            num_args,
            flags,
            nullptr,
            &SqliteUdf::template_proxy_raw<Func>,
            nullptr,
            nullptr,
            nullptr
        );
    }

    // ========================================================================
    // STATEFUL UDF REGISTRATION (SqliteExtState Integration)
    // ========================================================================

    /**
     * @brief Register a C++ scalar function bound to a shared state type (SqliteExtState<State>)
     * passing the state instance directly to SQLite's user_data (pApp) with automated xDestroy cleanup.
     * 
     * Usage:
     *   SqliteUdf::define_with_state<MyState, my_func>(db, "my_func", 2);
     */
    template <typename State, void (*Func)(SqliteContext, SqliteUdfArgs)>
    static int define_with_state(
        SqliteDatabaseView db,
        const char* name,
        int num_args = -1,
        bool deterministic = false
    ) {
        void* raw_state = SqliteExtState<State>::init(db.get());
        int flags = SQLITE_UTF8 | (deterministic ? SQLITE_DETERMINISTIC : 0);
        return sqlite3_create_function_v2(
            db.get(),
            name,
            num_args,
            flags,
            raw_state,
            &SqliteUdf::template_proxy_context<Func>,
            nullptr,
            nullptr,
            SqliteExtState<State>::destructor
        );
    }

    template <typename State, void (*Func)(SqliteContext&, SqliteUdfArgs)>
    static int define_with_state(
        SqliteDatabaseView db,
        const char* name,
        int num_args = -1,
        bool deterministic = false
    ) {
        void* raw_state = SqliteExtState<State>::init(db.get());
        int flags = SQLITE_UTF8 | (deterministic ? SQLITE_DETERMINISTIC : 0);
        return sqlite3_create_function_v2(
            db.get(),
            name,
            num_args,
            flags,
            raw_state,
            &SqliteUdf::template_proxy_context_ref<Func>,
            nullptr,
            nullptr,
            SqliteExtState<State>::destructor
        );
    }

    template <typename State, void (*Func)(sqlite3_context*, SqliteUdfArgs)>
    static int define_with_state(
        SqliteDatabaseView db,
        const char* name,
        int num_args = -1,
        bool deterministic = false
    ) {
        void* raw_state = SqliteExtState<State>::init(db.get());
        int flags = SQLITE_UTF8 | (deterministic ? SQLITE_DETERMINISTIC : 0);
        return sqlite3_create_function_v2(
            db.get(),
            name,
            num_args,
            flags,
            raw_state,
            &SqliteUdf::template_proxy_raw<Func>,
            nullptr,
            nullptr,
            SqliteExtState<State>::destructor
        );
    }

private:
    /**
     * @brief The internal C callback executing raw ScalarFuncRaw functions.
     */
    static void scalar_proxy_raw(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
        void* user_data = sqlite3_user_data(ctx);
        if (!user_data) {
            sqlite3_result_error(ctx, "Internal Error: Null UDF User Data", -1);
            return;
        }

        ScalarFuncRaw func = reinterpret_cast<ScalarFuncRaw>(user_data);
        func(ctx, SqliteUdfArgs(argc, argv));
    }

    /**
     * @brief The internal C callback executing ScalarFuncContext functions with SqliteContext.
     */
    static void scalar_proxy_context(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
        void* user_data = sqlite3_user_data(ctx);
        if (!user_data) {
            sqlite3_result_error(ctx, "Internal Error: Null UDF User Data", -1);
            return;
        }

        ScalarFuncContext func = reinterpret_cast<ScalarFuncContext>(user_data);
        SqliteContext sqlite_ctx(ctx);
        func(sqlite_ctx, SqliteUdfArgs(argc, argv));
    }

    /**
     * @brief The internal C callback executing ScalarFuncContextRef functions with SqliteContext&.
     */
    static void scalar_proxy_context_ref(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
        void* user_data = sqlite3_user_data(ctx);
        if (!user_data) {
            sqlite3_result_error(ctx, "Internal Error: Null UDF User Data", -1);
            return;
        }

        ScalarFuncContextRef func = reinterpret_cast<ScalarFuncContextRef>(user_data);
        SqliteContext sqlite_ctx(ctx);
        func(sqlite_ctx, SqliteUdfArgs(argc, argv));
    }

    /**
     * @brief Template proxy directly invoking a compile-time function pointer Func with SqliteContext.
     */
    template <void (*Func)(SqliteContext, SqliteUdfArgs)>
    static void template_proxy_context(sqlite3_context* raw_ctx, int argc, sqlite3_value** argv) {
        SqliteContext ctx(raw_ctx);
        Func(ctx, SqliteUdfArgs(argc, argv));
    }

    /**
     * @brief Template proxy directly invoking a compile-time function pointer Func with SqliteContext&.
     */
    template <void (*Func)(SqliteContext&, SqliteUdfArgs)>
    static void template_proxy_context_ref(sqlite3_context* raw_ctx, int argc, sqlite3_value** argv) {
        SqliteContext ctx(raw_ctx);
        Func(ctx, SqliteUdfArgs(argc, argv));
    }

    /**
     * @brief Template proxy directly invoking a compile-time function pointer Func with sqlite3_context*.
     */
    template <void (*Func)(sqlite3_context*, SqliteUdfArgs)>
    static void template_proxy_raw(sqlite3_context* raw_ctx, int argc, sqlite3_value** argv) {
        Func(raw_ctx, SqliteUdfArgs(argc, argv));
    }
};

#endif // SQLITE3_UDF_HPP
