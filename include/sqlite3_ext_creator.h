#ifndef SQLITE3_EXT_CREATOR_H
#define SQLITE3_EXT_CREATOR_H

#include "sqlite3ext.h"
#include <sqlite3.h>

// ============================================================================
// 1. GLOBAL DISPATCH TABLE INITIALIZATION
// ============================================================================
SQLITE_EXTENSION_INIT1

#include "sqlite3_ext.h"

// ============================================================================
// 2. CROSS-PLATFORM DYNAMIC SYMBOL EXPORT
// ============================================================================
#ifndef SQLITE_EXTENSION_EXPORT
    #if defined(_MSC_VER) || defined(_WIN32) || defined(__WIN32__) || defined(__CYGWIN__)
        #define SQLITE_EXTENSION_EXPORT __declspec(dllexport)
    #elif defined(__GNUC__) && __GNUC__ >= 4
        #define SQLITE_EXTENSION_EXPORT __attribute__((visibility("default")))
    #else
        #define SQLITE_EXTENSION_EXPORT
    #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 3. PURE C EXTENSION ENTRYPOINT MACROS
// ============================================================================

/**
 * @brief Generates a named Pure C SQLite extension entrypoint: `sqlite3_<ext_name>_init`.
 * 
 * Example Usage:
 * @code
 * SQLITE_C_EXTENSION_ENTRYPOINT(myext, db) {
 *     sqlite3_create_function(db, "my_add", 2, SQLITE_UTF8 | SQLITE_DETERMINISTIC, NULL, my_add_func, NULL, NULL);
 *     return SQLITE_OK;
 * }
 * @endcode
 * 
 * @param ext_name The name of the extension module (used in `sqlite3_<ext_name>_init`).
 * @param db_var   The variable name assigned to the `sqlite3*` database handle parameter.
 */
#define SQLITE_C_EXTENSION_ENTRYPOINT(ext_name, db_var) \
    static int __sqlite3_c_ext_entrypoint_impl_##ext_name(sqlite3 *db_var); \
    \
    SQLITE_EXTENSION_EXPORT int sqlite3_##ext_name##_init( \
        sqlite3 *db, \
        char **pzErrMsg, \
        const sqlite3_api_routines *pApi \
    ) { \
        SQLITE_EXTENSION_INIT2(pApi); \
        if (!pApi) { \
            if (pzErrMsg) *pzErrMsg = sqlite3_mprintf("SQLite extension API pointer is NULL"); \
            return SQLITE_ERROR; \
        } \
        return __sqlite3_c_ext_entrypoint_impl_##ext_name(db); \
    } \
    \
    static int __sqlite3_c_ext_entrypoint_impl_##ext_name(sqlite3 *db_var)

/**
 * @brief Generates the default Pure C SQLite extension entrypoint: `sqlite3_extension_init`.
 * 
 * Invoked by SQLite when an extension is loaded via `.load <file>` without an explicit proc name.
 * 
 * Example Usage:
 * @code
 * SQLITE_C_DEFAULT_EXTENSION_ENTRYPOINT(db) {
 *     sqlite3_create_function(db, "my_func", 1, SQLITE_UTF8, NULL, my_func, NULL, NULL);
 *     return SQLITE_OK;
 * }
 * @endcode
 * 
 * @param db_var The variable name assigned to the `sqlite3*` database handle parameter.
 */
#define SQLITE_C_DEFAULT_EXTENSION_ENTRYPOINT(db_var) \
    static int __sqlite3_c_default_ext_entrypoint_impl(sqlite3 *db_var); \
    \
    SQLITE_EXTENSION_EXPORT int sqlite3_extension_init( \
        sqlite3 *db, \
        char **pzErrMsg, \
        const sqlite3_api_routines *pApi \
    ) { \
        SQLITE_EXTENSION_INIT2(pApi); \
        if (!pApi) { \
            if (pzErrMsg) *pzErrMsg = sqlite3_mprintf("SQLite extension API pointer is NULL"); \
            return SQLITE_ERROR; \
        } \
        return __sqlite3_c_default_ext_entrypoint_impl(db); \
    } \
    \
    static int __sqlite3_c_default_ext_entrypoint_impl(sqlite3 *db_var)

/**
 * @brief Generates a named Pure C entrypoint receiving both `sqlite3* db` and `char** pzErrMsg`.
 * 
 * Useful when initialization failure needs to set a custom error message using `sqlite3_mprintf`.
 * 
 * @param ext_name The name of the extension module.
 * @param db_var   Variable name for `sqlite3*`.
 * @param err_var  Variable name for `char** pzErrMsg`.
 */
#define SQLITE_C_EXTENSION_ENTRYPOINT_ERR(ext_name, db_var, err_var) \
    static int __sqlite3_c_ext_entrypoint_err_impl_##ext_name(sqlite3 *db_var, char **err_var); \
    \
    SQLITE_EXTENSION_EXPORT int sqlite3_##ext_name##_init( \
        sqlite3 *db, \
        char **pzErrMsg, \
        const sqlite3_api_routines *pApi \
    ) { \
        SQLITE_EXTENSION_INIT2(pApi); \
        if (!pApi) { \
            if (pzErrMsg) *pzErrMsg = sqlite3_mprintf("SQLite extension API pointer is NULL"); \
            return SQLITE_ERROR; \
        } \
        return __sqlite3_c_ext_entrypoint_err_impl_##ext_name(db, pzErrMsg); \
    } \
    \
    static int __sqlite3_c_ext_entrypoint_err_impl_##ext_name(sqlite3 *db_var, char **err_var)

#ifdef __cplusplus
}
#endif

#endif // SQLITE3_EXT_CREATOR_H
