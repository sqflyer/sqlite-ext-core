#ifndef SQLITE3_UDF_HPP
#define SQLITE3_UDF_HPP

#include <sqlite3.h>
#include "sqlite3_value.hpp"
#include "sqlite3_aggregate.hpp"
#include "sqlite3_tvf.hpp"

/**
 * @brief Lightweight registry for mapping C++ stateless functions/lambdas and aggregates to SQLite UDFs.
 */
class SqliteUdf {
public:
    /**
     * @brief The C++ scalar function signature.
     * 
     * BEST PRACTICE FOR RESULTS:
     * When writing scalar functions, you must explicitly set the result on the `ctx` before returning.
     * While you can use the raw C-APIs (`sqlite3_result_int`, `sqlite3_result_text`), it is highly 
     * recommended to use the wrapper types' `.result(ctx)` method when dealing with dynamic memory 
     * (Strings and Blobs). This prevents memory leaks and simplifies ownership.
     * 
     * Example:
     *   SqliteStringOwned str(ctx);
     *   str.appendall("hello");
     *   str.result(ctx); // Automatically calls sqlite3_result_text and safely transfers memory ownership!
     */
    typedef void (*ScalarFunc)(sqlite3_context* ctx, SqliteUdfArgs args);

    /**
     * @brief Register a C++ scalar function with SQLite.
     * 
     * @param db The SQLite database connection.
     * @param name The SQL name of the function.
     * @param num_args Expected argument count (-1 for variadic).
     * @param func A C++ function pointer or stateless lambda matching ScalarFunc.
     * @param deterministic Whether the function is deterministic (default true).
     * @return SQLITE_OK on success, or an error code.
     */
    static int define(sqlite3* db, const char* name, int num_args, ScalarFunc func, bool deterministic = true) {
        int flags = SQLITE_UTF8;
        if (deterministic) {
            flags |= SQLITE_DETERMINISTIC;
        }

        // We pass the C++ function pointer directly into sqlite3_user_data!
        return sqlite3_create_function_v2(
            db,
            name,
            num_args,
            flags,
            reinterpret_cast<void*>(func),
            &SqliteUdf::scalar_proxy,
            nullptr, // xStep
            nullptr, // xFinal
            nullptr  // xDestroy
        );
    }

    /**
     * @brief Register an Object-Oriented C++ Aggregate Function struct with SQLite.
     * 
     * @tparam T The aggregate struct/class implementing step() and finalize().
     * @param db The SQLite database connection.
     * @param name The SQL name of the aggregate function.
     * @param num_args Expected argument count (-1 for variadic).
     * @param deterministic Whether the aggregate is deterministic (default true).
     * @return SQLITE_OK on success, or an error code.
     */
    template <typename T>
    static inline int define_aggregate(sqlite3* db, const char* name, int num_args = -1, bool deterministic = true) {
        return SqliteAggregate<T>::define(db, name, num_args, deterministic);
    }

    /**
     * @brief Register an Object-Oriented C++ Table-Valued Function (TVF) with SQLite.
     * 
     * @tparam T The iterator struct/class inheriting from SqliteTvfIterator.
     * @param db The SQLite database connection.
     * @param name The SQL name of the TVF.
     * @return SQLITE_OK on success, or an error code.
     */
    template <typename T>
    static inline int define_tvf(sqlite3* db, const char* name) {
        return SqliteTvfModule<T>::define(db, name);
    }

private:
    /**
     * @brief The internal C callback that SQLite executes.
     * 
     * This safely extracts the user's C++ function from the user_data pointer,
     * wraps the ugly argv array in SqliteUdfArgs, and invokes the C++ function.
     */
    static void scalar_proxy(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
        // Extract the C++ function pointer
        void* user_data = sqlite3_user_data(ctx);
        if (!user_data) {
            sqlite3_result_error(ctx, "Internal Error: Null UDF User Data", -1);
            return;
        }

        ScalarFunc func = reinterpret_cast<ScalarFunc>(user_data);
        
        // Wrap arguments and call C++ function
        func(ctx, SqliteUdfArgs(argc, argv));
    }
};

#endif // SQLITE3_UDF_HPP
