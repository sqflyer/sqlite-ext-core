#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1

#include "../../../include/sqlite3_ext_state.h"

// Define our state
typedef struct {
    int counter;
} SharedState;

// Generate the C++ compatible thread-safe registry and RAII guards
SQLITE_EXTENSION_STATE(SharedState)

static void test_counter_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    SharedState *state = SharedState_from_context(ctx);
    if (!state) return;
    
    // Example 1: RAII Write Lock
    {
        SharedState_WriteGuard lock(state);
        lock->counter++;
        // lock is automatically released when this block goes out of scope
    }
    
    // Example 2: RAII Read Lock
    int val = 0;
    {
        SharedState_ReadGuard lock(state);
        val = lock->counter;
        // lock is automatically released when this block goes out of scope
    }
    
    sqlite3_result_int64(ctx, (sqlite3_int64)val);
}

// C++ requires extern "C" for the SQLite entrypoint
#ifdef _WIN32
extern "C" __declspec(dllexport)
#else
extern "C"
#endif
int sqlite3_myext_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi) {
    SQLITE_EXTENSION_INIT2(pApi);
    if (!pApi) return 1;

    void* raw_state = SharedState_init(db, NULL, NULL);
    return sqlite3_create_function_v2(db, "test_counter", 0, SQLITE_UTF8, raw_state, test_counter_func, NULL, NULL, SharedState_destructor);
}
