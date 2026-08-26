#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1

#include "../../../include/sqlite3_ext_state.h"

typedef struct {
    int counter;
} SharedState;

/*
 * The macros below automatically expand into the following thread-safe internal API:
 * 
 * DECLARE (Safe for headers):
 *   typedef struct SharedState_Entry { ... } SharedState_Entry;
 *   extern SharedState_Entry *SharedState_registry_head;
 *   extern sqlite3_mutex     *SharedState_registry_mutex;
 * 
 *   // The 8 user-facing API functions:
 *   static void*         SharedState_init(sqlite3 *db, void (*init_fn)(SharedState*), void (*free_fn)(SharedState*));
 *   static void          SharedState_destructor(void *p);
 *   static SharedState*  SharedState_from_db(sqlite3_context *ctx, sqlite3 *db);
 *   static SharedState*  SharedState_from_context(sqlite3_context *ctx);
 *   static void          SharedState_read_acquire(SharedState *state);
 *   static void          SharedState_read_release(SharedState *state);
 *   static void          SharedState_write_acquire(SharedState *state);
 *   static void          SharedState_write_release(SharedState *state);
 * 
 * DEFINE (Must be in exactly ONE .c file):
 *   SharedState_Entry *SharedState_registry_head = NULL;
 *   sqlite3_mutex     *SharedState_registry_mutex = NULL;
 *   // ... and internal memory/mutex lifecycle helpers
 */
SQLITE_EXTENSION_STATE_DECLARE(SharedState)
SQLITE_EXTENSION_STATE_DEFINE(SharedState)

static void test_counter_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    (void)argc;
    (void)argv;
    SharedState *state = SharedState_from_context(ctx);
    if (!state) return;
    
    SharedState_write_acquire(state);
    state->counter++;
    SharedState_write_release(state);
    
    SharedState_read_acquire(state);
    int val = state->counter;
    SharedState_read_release(state);
    
    sqlite3_result_int64(ctx, (sqlite3_int64)val);
}

static void test_counter_from_db_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    (void)argc;
    (void)argv;
    sqlite3 *db = sqlite3_context_db_handle(ctx);
    SharedState *state = SharedState_from_db(ctx, db);
    if (!state) return;
    
    SharedState_write_acquire(state);
    state->counter += 10;
    SharedState_write_release(state);
    
    SharedState_read_acquire(state);
    int val = state->counter;
    SharedState_read_release(state);
    
    sqlite3_result_int64(ctx, (sqlite3_int64)val);
}

static void my_init_fn(SharedState* state) {
    state->counter = 100;
}

static void my_free_fn(SharedState* state) {
    state->counter = -1; // Prove it runs before free
}

#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_myext_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi) {
    (void)pzErrMsg;
    SQLITE_EXTENSION_INIT2(pApi);
    if (!pApi) return 1;

    void* raw_state = SharedState_init(db, my_init_fn, my_free_fn);
    int rc = sqlite3_create_function_v2(db, "test_counter", 0, SQLITE_UTF8, raw_state, test_counter_func, NULL, NULL, SharedState_destructor);
    if (rc != SQLITE_OK) return rc;
    
    return sqlite3_create_function_v2(db, "test_counter_from_db", 0, SQLITE_UTF8, raw_state, test_counter_from_db_func, NULL, NULL, NULL);
}
