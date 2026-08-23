#ifndef SQLITE3_EXT_CREATOR_HPP
#define SQLITE3_EXT_CREATOR_HPP

#include "sqlite3ext.h"
#include <sqlite3.h>

// ============================================================================
// 1. GLOBAL DISPATCH TABLE INITIALIZATION
// ============================================================================
// When compiling as a dynamically loadable module (shared object / DLL),
// SQLite requires declaring the global dispatch routine pointer `sqlite3_api`.
// Placing SQLITE_EXTENSION_INIT1 here ensures that all subsequent header files
// and C++ templates (UDF, VTab, TVF, Db, Statements) can safely invoke SQLite
// API routines without encountering undeclared symbol compiler errors.
SQLITE_EXTENSION_INIT1

#include "sqlite3_ext.hpp"

// ============================================================================
// 2. CROSS-PLATFORM DYNAMIC SYMBOL EXPORT
// ============================================================================
// In order for SQLite's dynamic loader (`sqlite3_load_extension` or `.load`)
// to discover the extension's entrypoint function in a shared library (.dll / .so),
// the symbol must be explicitly exported without C++ name mangling:
// - Windows (MSVC/MinGW): Requires `__declspec(dllexport)`
// - Unix (GCC / Clang):   Requires `__attribute__((visibility("default")))`
#ifndef SQLITE_EXTENSION_EXPORT
    #if defined(_MSC_VER) || defined(_WIN32) || defined(__WIN32__) || defined(__CYGWIN__)
        #define SQLITE_EXTENSION_EXPORT __declspec(dllexport)
    #elif defined(__GNUC__) && __GNUC__ >= 4
        #define SQLITE_EXTENSION_EXPORT __attribute__((visibility("default")))
    #else
        #define SQLITE_EXTENSION_EXPORT
    #endif
#endif

// ============================================================================
// 3. INITIALIZATION CONTEXT
// ============================================================================
/**
 * @brief Zero-overhead context wrapper passed to extension entrypoints.
 * 
 * Encapsulates the raw `sqlite3*` database handle and SQLite's `char** pzErrMsg` 
 * pointer. Provides modern `SqliteDatabaseView` conversion and convenient
 * error reporting via `set_error()`.
 */
class SqliteExtensionInitContext {
private:
    sqlite3* m_db;
    char** m_pzErrMsg;

public:
    inline SqliteExtensionInitContext(sqlite3* db, char** pzErrMsg)
        : m_db(db), m_pzErrMsg(pzErrMsg) {}

    /** @brief Get the raw underlying sqlite3* database connection handle. */
    inline sqlite3* raw_db() const { return m_db; }

    /** @brief Get the modern RAII SqliteDatabaseView non-owning wrapper. */
    inline SqliteDatabaseView db() const { return SqliteDatabaseView(m_db); }
    inline operator sqlite3*() const { return m_db; }
    inline operator SqliteDatabaseView() const { return SqliteDatabaseView(m_db); }

    /**
     * @brief Formats and assigns a descriptive error message upon initialization failure.
     * 
     * SQLite requires extension init error strings to be allocated via `sqlite3_mprintf`.
     * This method formats the message and writes it directly to SQLite's `pzErrMsg` output pointer.
     * 
     * @param message Null-terminated error string.
     */
    inline void set_error(const char* message) const {
        if (m_pzErrMsg && message) {
            *m_pzErrMsg = sqlite3_mprintf("%s", message);
        }
    }
};

// ============================================================================
// 4. EXTENSION ENTRYPOINT MACROS
// ============================================================================

/**
 * @brief Generates a named SQLite extension entrypoint: `sqlite3_<ext_name>_init`.
 * 
 * HOW IT WORKS UNDER THE HOOD:
 * ----------------------------------------------------------------------------
 * 1. Forward-declares a static implementation function:
 *      `int __sqlite3_ext_entrypoint_impl_<ext_name>(SqliteDatabaseView <db_var>)`
 *    This ensures your C++ registration logic remains private to this translation unit.
 * 
 * 2. Emits the `extern "C"` ABI-compliant exported function:
 *      `int sqlite3_<ext_name>_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi)`
 * 
 * 3. Inside the exported trampoline:
 *    a. Calls `SQLITE_EXTENSION_INIT2(pApi)` to bind SQLite's routine dispatch table.
 *    b. Checks that `pApi` is valid; returns `SQLITE_ERROR` with a message if null.
 *    c. Wraps `db` in `SqliteDatabaseView` and passes control to your implementation block.
 * 
 * 4. Begins the definition of the static implementation function, allowing you to
 *    immediately write the function body `{ ... }`.
 * ----------------------------------------------------------------------------
 * 
 * Example Usage:
 * @code
 * SQLITE_EXTENSION_ENTRYPOINT(myext, db) {
 *     // Register scalar functions, TVFs, and virtual tables using modern C++ APIs:
 *     SqliteUdf::define(db, "my_add", 2, my_add_func);
 *     return SQLITE_OK;
 * }
 * @endcode
 * 
 * @param ext_name The name of the extension module (used in `sqlite3_<ext_name>_init`).
 * @param db_var   The variable name assigned to the `SqliteDatabaseView` parameter.
 */
#define SQLITE_EXTENSION_ENTRYPOINT(ext_name, db_var) \
    /* Step 1: Forward-declare the private static C++ implementation */ \
    static int __sqlite3_ext_entrypoint_impl_##ext_name(SqliteDatabaseView db_var); \
    \
    /* Step 2: Emit the extern "C" dynamic entrypoint required by SQLite's loader */ \
    extern "C" SQLITE_EXTENSION_EXPORT int sqlite3_##ext_name##_init( \
        sqlite3 *db, \
        char **pzErrMsg, \
        const sqlite3_api_routines *pApi \
    ) { \
        /* Step 3a: Initialize the SQLite API routine dispatch table */ \
        SQLITE_EXTENSION_INIT2(pApi); \
        \
        /* Step 3b: Validate that SQLite provided a valid dispatch table */ \
        if (!pApi) { \
            if (pzErrMsg) *pzErrMsg = sqlite3_mprintf("SQLite extension API pointer is NULL"); \
            return SQLITE_ERROR; \
        } \
        \
        /* Step 3c: Invoke user implementation with a SqliteDatabaseView wrapper */ \
        return __sqlite3_ext_entrypoint_impl_##ext_name(SqliteDatabaseView(db)); \
    } \
    \
    /* Step 4: Open definition of user implementation block */ \
    static int __sqlite3_ext_entrypoint_impl_##ext_name(SqliteDatabaseView db_var)

/**
 * @brief Generates the default SQLite extension entrypoint: `sqlite3_extension_init`.
 * 
 * SQLite invokes this default entrypoint when an extension is loaded via `.load <file>`
 * or `sqlite3_load_extension(db, file, NULL, ...)` without specifying an explicit proc name.
 * 
 * Example Usage:
 * @code
 * SQLITE_DEFAULT_EXTENSION_ENTRYPOINT(db) {
 *     SqliteUdf::define(db, "my_func", 1, my_func);
 *     return SQLITE_OK;
 * }
 * @endcode
 * 
 * @param db_var The variable name assigned to the `SqliteDatabaseView` parameter.
 */
#define SQLITE_DEFAULT_EXTENSION_ENTRYPOINT(db_var) \
    /* Step 1: Forward-declare the default static implementation */ \
    static int __sqlite3_default_ext_entrypoint_impl(SqliteDatabaseView db_var); \
    \
    /* Step 2: Emit the default exported entrypoint: sqlite3_extension_init */ \
    extern "C" SQLITE_EXTENSION_EXPORT int sqlite3_extension_init( \
        sqlite3 *db, \
        char **pzErrMsg, \
        const sqlite3_api_routines *pApi \
    ) { \
        /* Step 3a: Initialize the SQLite API routine dispatch table */ \
        SQLITE_EXTENSION_INIT2(pApi); \
        \
        /* Step 3b: Validate dispatch table pointer */ \
        if (!pApi) { \
            if (pzErrMsg) *pzErrMsg = sqlite3_mprintf("SQLite extension API pointer is NULL"); \
            return SQLITE_ERROR; \
        } \
        \
        /* Step 3c: Invoke user implementation */ \
        return __sqlite3_default_ext_entrypoint_impl(SqliteDatabaseView(db)); \
    } \
    \
    /* Step 4: Open definition of default user implementation block */ \
    static int __sqlite3_default_ext_entrypoint_impl(SqliteDatabaseView db_var)

/**
 * @brief Generates an entrypoint receiving `SqliteExtensionInitContext` directly.
 * 
 * Useful when initialization needs direct access to formatted error strings (`ctx.set_error()`)
 * or raw pointers in addition to database helper methods.
 * 
 * Example Usage:
 * @code
 * SQLITE_EXTENSION_ENTRYPOINT_CTX(myext, ctx) {
 *     if (!initialize_dependencies()) {
 *         ctx.set_error("Dependency initialization failed");
 *         return SQLITE_ERROR;
 *     }
 *     SqliteUdf::define(ctx.db(), "my_func", 1, my_func);
 *     return SQLITE_OK;
 * }
 * @endcode
 * 
 * @param ext_name The name of the extension module.
 * @param ctx_var  The variable name assigned to the `SqliteExtensionInitContext` parameter.
 */
#define SQLITE_EXTENSION_ENTRYPOINT_CTX(ext_name, ctx_var) \
    /* Step 1: Forward-declare context-aware static implementation */ \
    static int __sqlite3_ext_entrypoint_ctx_impl_##ext_name(SqliteExtensionInitContext& ctx_var); \
    \
    /* Step 2: Emit the exported entrypoint */ \
    extern "C" SQLITE_EXTENSION_EXPORT int sqlite3_##ext_name##_init( \
        sqlite3 *db, \
        char **pzErrMsg, \
        const sqlite3_api_routines *pApi \
    ) { \
        /* Step 3a: Initialize the SQLite API routine dispatch table */ \
        SQLITE_EXTENSION_INIT2(pApi); \
        \
        /* Step 3b: Validate dispatch table pointer */ \
        if (!pApi) { \
            if (pzErrMsg) *pzErrMsg = sqlite3_mprintf("SQLite extension API pointer is NULL"); \
            return SQLITE_ERROR; \
        } \
        \
        /* Step 3c: Instantiate context and invoke user implementation */ \
        SqliteExtensionInitContext ctx_var(db, pzErrMsg); \
        return __sqlite3_ext_entrypoint_ctx_impl_##ext_name(ctx_var); \
    } \
    \
    /* Step 4: Open definition of context-aware user implementation block */ \
    static int __sqlite3_ext_entrypoint_ctx_impl_##ext_name(SqliteExtensionInitContext& ctx_var)

#endif // SQLITE3_EXT_CREATOR_HPP
