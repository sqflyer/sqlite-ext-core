#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1

#include "../../../include/sqlite3_ext_state.hpp"

// Define our state
struct SharedState {
    int counter;
};

static void test_counter_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    SharedState *state = SqliteExtState<SharedState>::from_context(ctx);
    if (!state) return;
    
    // Example 1: RAII Write Lock
    {
        SqliteExtState<SharedState>::WriteGuard lock(state);
        lock->counter++;
        // lock is automatically released when this block goes out of scope
    }
    
    // Example 2: RAII Read Lock
    int val = 0;
    {
        SqliteExtState<SharedState>::ReadGuard lock(state);
        val = lock->counter;
        // lock is automatically released when this block goes out of scope
    }
    
    sqlite3_result_int64(ctx, (sqlite3_int64)val);
}

static void test_counter_from_db_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    sqlite3 *db = sqlite3_context_db_handle(ctx);
    SharedState *state = SqliteExtState<SharedState>::from_db(ctx, db);
    if (!state) return;
    
    {
        SqliteExtState<SharedState>::write_acquire(state);
        state->counter += 10;
        SqliteExtState<SharedState>::write_release(state);
    }
    
    int val = 0;
    {
        SqliteExtState<SharedState>::read_acquire(state);
        val = state->counter;
        SqliteExtState<SharedState>::read_release(state);
    }
    
    sqlite3_result_int64(ctx, (sqlite3_int64)val);
}

// C++ requires extern "C" for the SQLite entrypoint
static void my_init_fn(SharedState* state) {
    state->counter = 100;
}


#ifdef _WIN32
extern "C" __declspec(dllexport)
#else
extern "C"
#endif
int sqlite3_myext_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi) {
    SQLITE_EXTENSION_INIT2(pApi);
    if (!pApi) return 1;

    void* raw_state = SqliteExtState<SharedState>::init(db, my_init_fn);
    int rc = sqlite3_create_function_v2(db, "test_counter", 0, SQLITE_UTF8, raw_state, test_counter_func, NULL, NULL, SqliteExtState<SharedState>::destructor);
    if (rc != SQLITE_OK) return rc;
    
    return sqlite3_create_function_v2(db, "test_counter_from_db", 0, SQLITE_UTF8, raw_state, test_counter_from_db_func, NULL, NULL, NULL);
}
