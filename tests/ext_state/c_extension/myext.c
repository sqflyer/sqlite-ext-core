#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1

#include "../../../include/sqlite3_ext_state.h"

typedef struct {
    int counter;
} SharedState;

/*
 * The macro below automatically expands into the following thread-safe internal API:
 * 
 *   typedef struct SharedState_Entry { ... } SharedState_Entry;
 *   
 *   static SharedState_Entry *SharedState_registry_head = NULL;
 *   static sqlite3_mutex     *SharedState_registry_mutex = NULL;
 * 
 *   // Internal helpers (prefixed with __ to hide from autocomplete)
 *   static const char*        __SharedState_get_db_path(sqlite3 *db, char *buf);
 *   static void               __SharedState_ensure_mutex_init(void);
 *   static SharedState_Entry* __SharedState_entry_retain(SharedState_Entry *entry);
 *   static void               __SharedState_entry_release(SharedState_Entry *entry);
 *   static void               __SharedState_entry_free(SharedState_Entry *entry);
 *   // ... and other internal memory/mutex lifecycle helpers
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
 */
SQLITE_EXTENSION_STATE(SharedState)

static void test_counter_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
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

#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_myext_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi) {
    SQLITE_EXTENSION_INIT2(pApi);
    if (!pApi) return 1;

    void* raw_state = SharedState_init(db, NULL, NULL);
    return sqlite3_create_function_v2(db, "test_counter", 0, SQLITE_UTF8, raw_state, test_counter_func, NULL, NULL, SharedState_destructor);
}
