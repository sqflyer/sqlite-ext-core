#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1

#include "../../../include/sqlite3_hybrid_state.hpp"

// 1. Per-Database Shared State
struct SharedDbState {
    int global_counter = 0;
};

// 2. Per-Connection Private State
struct ConnSessionState {
    int local_counter = 0;
};

// 3. Unified Hybrid State
using AppHybrid = SqliteHybridState<SharedDbState, ConnSessionState, SqliteRwLock>;

// Scalar UDF: increments private per-connection counter (lock-free)
static void test_conn_counter_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    (void)argc;
    (void)argv;
    auto conn = AppHybrid::conn(ctx);
    if (!conn) return;

    conn->local_counter++;
    sqlite3_result_int64(ctx, static_cast<sqlite3_int64>(conn->local_counter));
}

// Scalar UDF: increments shared per-database counter (under write lock on ext state)
static void test_ext_counter_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    (void)argc;
    (void)argv;
    int val = 0;
    {
        AppHybrid::WriteGuard lock(ctx);
        if (!lock) return;
        lock->global_counter++;
        val = lock->global_counter;
    }

    sqlite3_result_int64(ctx, static_cast<sqlite3_int64>(val));
}

// Scalar UDF: queries both states via conn(db) and ReadGuard(db)
static void test_hybrid_from_db_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    (void)argc;
    (void)argv;
    sqlite3 *db = sqlite3_context_db_handle(ctx);
    auto conn = AppHybrid::conn(db);
    if (!conn) return;

    int ext_val = 0;
    {
        AppHybrid::ReadGuard lock(db);
        if (!lock) return;
        ext_val = lock->global_counter;
    }

    int conn_val = conn->local_counter;

    // Encode both into 64-bit integer: (ext * 1000000) + conn
    sqlite3_int64 result = (static_cast<sqlite3_int64>(ext_val) * 1000000LL) + static_cast<sqlite3_int64>(conn_val);
    sqlite3_result_int64(ctx, result);
}

#ifdef _WIN32
extern "C" __declspec(dllexport)
#else
extern "C"
#endif
int sqlite3_myext_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi) {
    (void)pzErrMsg;
    SQLITE_EXTENSION_INIT2(pApi);
    if (!pApi) return 1;

    void* holder1 = AppHybrid::init(
        db,
        [](SharedDbState* s) { s->global_counter = 1000; },
        [](ConnSessionState* s) { s->local_counter = 100; }
    );
    if (!holder1) return SQLITE_NOMEM;

    void* holder2 = AppHybrid::init(
        db,
        [](SharedDbState* s) { s->global_counter = 1000; },
        [](ConnSessionState* s) { s->local_counter = 100; }
    );
    if (!holder2) {
        AppHybrid::destructor(holder1);
        return SQLITE_NOMEM;
    }

    void* holder3 = AppHybrid::init(
        db,
        [](SharedDbState* s) { s->global_counter = 1000; },
        [](ConnSessionState* s) { s->local_counter = 100; }
    );
    if (!holder3) {
        AppHybrid::destructor(holder1);
        AppHybrid::destructor(holder2);
        return SQLITE_NOMEM;
    }

    int rc = sqlite3_create_function_v2(db, "test_conn_counter", 0, SQLITE_UTF8, holder1, test_conn_counter_func, nullptr, nullptr, AppHybrid::destructor);
    if (rc != SQLITE_OK) return rc;

    rc = sqlite3_create_function_v2(db, "test_ext_counter", 0, SQLITE_UTF8, holder2, test_ext_counter_func, nullptr, nullptr, AppHybrid::destructor);
    if (rc != SQLITE_OK) return rc;

    return sqlite3_create_function_v2(db, "test_hybrid_from_db", 0, SQLITE_UTF8, holder3, test_hybrid_from_db_func, nullptr, nullptr, AppHybrid::destructor);
}
