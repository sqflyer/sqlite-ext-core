#ifndef SQLITE3_UDF_HPP
#define SQLITE3_UDF_HPP

#include <sqlite3.h>
#include "sqlite3_value.hpp"

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
     * @brief Get the number of arguments passed to the UDF.
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

/**
 * @brief Lightweight registry for mapping C++ stateless functions/lambdas to SQLite UDFs.
 */
class SqliteUdf {
public:
    /**
     * @brief The C++ scalar function signature.
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
