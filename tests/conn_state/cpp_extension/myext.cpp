#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1

#include "../../../include/sqlite3_conn_state.hpp"

// Define our connection state
struct ConnState {
    int counter;
};

static void test_counter_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    (void)argc;
    (void)argv;
    ConnState *state = SqliteConnState<ConnState>::from_context(ctx);
    if (!state) return;
    
    // Lock-free connection state update
    state->counter++;
    sqlite3_result_int64(ctx, (sqlite3_int64)state->counter);
}

static void test_counter_from_db_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    (void)argc;
    (void)argv;
    sqlite3 *db = sqlite3_context_db_handle(ctx);
    ConnState *state = SqliteConnState<ConnState>::from_db(db);
    if (!state) return;
    
    // Lock-free connection state update
    state->counter += 10;
    sqlite3_result_int64(ctx, (sqlite3_int64)state->counter);
}

static void my_init_fn(ConnState* state) {
    state->counter = 100;
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

    void* raw_state = SqliteConnState<ConnState>::init(db, my_init_fn);
    if (!raw_state) return SQLITE_NOMEM;

    int rc = sqlite3_create_function_v2(db, "test_counter", 0, SQLITE_UTF8, raw_state, test_counter_func, NULL, NULL, SqliteConnState<ConnState>::destructor);
    if (rc != SQLITE_OK) return rc;
    
    return sqlite3_create_function_v2(db, "test_counter_from_db", 0, SQLITE_UTF8, raw_state, test_counter_from_db_func, NULL, NULL, NULL);
}
