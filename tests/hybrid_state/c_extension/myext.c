#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1

#include "../../../include/sqlite3_hybrid_state.h"

// 1. Per-Database Shared State
typedef struct {
    int global_counter;
} SharedDbState;

SQLITE_EXTENSION_STATE_DECLARE(SharedDbState)
SQLITE_EXTENSION_STATE_DEFINE(SharedDbState)

// 2. Per-Connection Private State
typedef struct {
    int local_counter;
} ConnSessionState;

SQLITE_CONNECTION_STATE(ConnSessionState)

// 3. Unified Hybrid State
SQLITE_HYBRID_STATE(SharedDbState, ConnSessionState, AppHybrid)

// Scalar UDF: increments private per-connection counter (lock-free)
static void test_conn_counter_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    (void)argc;
    (void)argv;
    ConnSessionState *conn = AppHybrid_hybrid_conn(ctx);
    if (!conn) return;

    conn->local_counter++;
    sqlite3_result_int64(ctx, (sqlite3_int64)conn->local_counter);
}

// Scalar UDF: increments shared per-database counter (under write lock on ext state)
static void test_ext_counter_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    (void)argc;
    (void)argv;
    SharedDbState *ext = AppHybrid_hybrid_ext(ctx);
    if (!ext) return;

    AppHybrid_hybrid_write_acquire(ext);
    ext->global_counter++;
    int val = ext->global_counter;
    AppHybrid_hybrid_write_release(ext);

    sqlite3_result_int64(ctx, (sqlite3_int64)val);
}

// Scalar UDF: queries both states via conn_from_db and ext_from_db
static void test_hybrid_from_db_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    (void)argc;
    (void)argv;
    sqlite3 *db = sqlite3_context_db_handle(ctx);
    ConnSessionState *conn = AppHybrid_hybrid_conn_from_db(db);
    SharedDbState *ext = AppHybrid_hybrid_ext_from_db(db);
    if (!ext || !conn) return;

    AppHybrid_hybrid_read_acquire(ext);
    int ext_val = ext->global_counter;
    AppHybrid_hybrid_read_release(ext);

    int conn_val = conn->local_counter;

    // Encode both into 64-bit integer: (ext * 1000000) + conn
    sqlite3_int64 result = ((sqlite3_int64)ext_val * 1000000LL) + (sqlite3_int64)conn_val;
    sqlite3_result_int64(ctx, result);
}

static void my_ext_init(SharedDbState *s) {
    s->global_counter = 1000;
}

static void my_ext_free(SharedDbState *s) {
    s->global_counter = -1;
}

static void my_conn_init(ConnSessionState *s) {
    s->local_counter = 100;
}

static void my_conn_free(ConnSessionState *s) {
    s->local_counter = -1;
}

#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_myext_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi) {
    (void)pzErrMsg;
    SQLITE_EXTENSION_INIT2(pApi);
    if (!pApi) return 1;

    // Dual registration testing multi-function reference counting
    void* holder1 = AppHybrid_hybrid_init(db, my_ext_init, my_ext_free, my_conn_init, my_conn_free);
    if (!holder1) return SQLITE_NOMEM;

    void* holder2 = AppHybrid_hybrid_init(db, my_ext_init, my_ext_free, my_conn_init, my_conn_free);
    if (!holder2) {
        AppHybrid_hybrid_destructor(holder1);
        return SQLITE_NOMEM;
    }

    void* holder3 = AppHybrid_hybrid_init(db, my_ext_init, my_ext_free, my_conn_init, my_conn_free);
    if (!holder3) {
        AppHybrid_hybrid_destructor(holder1);
        AppHybrid_hybrid_destructor(holder2);
        return SQLITE_NOMEM;
    }

    int rc = sqlite3_create_function_v2(db, "test_conn_counter", 0, SQLITE_UTF8, holder1, test_conn_counter_func, NULL, NULL, AppHybrid_hybrid_destructor);
    if (rc != SQLITE_OK) return rc;

    rc = sqlite3_create_function_v2(db, "test_ext_counter", 0, SQLITE_UTF8, holder2, test_ext_counter_func, NULL, NULL, AppHybrid_hybrid_destructor);
    if (rc != SQLITE_OK) return rc;

    return sqlite3_create_function_v2(db, "test_hybrid_from_db", 0, SQLITE_UTF8, holder3, test_hybrid_from_db_func, NULL, NULL, AppHybrid_hybrid_destructor);
}
