#define SQLITE_CORE
#include "../../include/sqlite3_ext.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// 1. Define Shared State
typedef struct {
    int global_counter;
    char cluster_name[32];
} AppExtState;

SQLITE_EXTENSION_STATE_DECLARE(AppExtState)
SQLITE_EXTENSION_STATE_DEFINE(AppExtState)

// 2. Define Connection State
typedef struct {
    int session_id;
    int local_query_count;
} AppConnState;

SQLITE_CONNECTION_STATE(AppConnState)

// 3. Define Hybrid State
SQLITE_HYBRID_STATE(AppExtState, AppConnState, AppHybrid)

// Scalar UDF accessing the unified Hybrid struct
static void test_hybrid_udf(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    (void)argc;
    (void)argv;
    AppHybrid_HybridState state = AppHybrid_hybrid_from_context(ctx);
    assert(state.ext != NULL);
    assert(state.conn != NULL);

    // Lock-free mutation on connection state:
    state.conn->local_query_count++;

    // Locked mutation on shared state:
    AppHybrid_hybrid_write_acquire(&state);
    state.ext->global_counter++;
    int total = state.ext->global_counter;
    AppHybrid_hybrid_write_release(&state);

    char buf[64];
    snprintf(buf, sizeof(buf), "conn=%d,ext=%d", state.conn->local_query_count, total);
    sqlite3_result_text(ctx, buf, -1, SQLITE_TRANSIENT);
}

// Initializer callbacks
static void init_ext(AppExtState *s) {
    s->global_counter = 100;
    snprintf(s->cluster_name, sizeof(s->cluster_name), "cluster_alpha");
}
static void init_conn1(AppConnState *s) {
    s->session_id = 1;
    s->local_query_count = 0;
}
static void init_conn2(AppConnState *s) {
    s->session_id = 2;
    s->local_query_count = 0;
}

void test_pure_c_hybrid_state() {
    printf("--- Running test_pure_c_hybrid_state ---\n");
    const char *db_file = "c_hybrid_test_db.sqlite";
    remove(db_file);

    sqlite3 *db1 = NULL;
    sqlite3 *db2 = NULL;

    assert(sqlite3_open(db_file, &db1) == SQLITE_OK);
    assert(sqlite3_open(db_file, &db2) == SQLITE_OK);

    // Initialize hybrid state on db1
    void *holder1 = AppHybrid_hybrid_init(db1, init_ext, NULL, init_conn1, NULL);
    assert(holder1 != NULL);
    assert(sqlite3_create_function_v2(
        db1, "hybrid_query", 0, SQLITE_UTF8,
        holder1,
        test_hybrid_udf,
        NULL, NULL,
        AppHybrid_hybrid_destructor
    ) == SQLITE_OK);

    // Initialize hybrid state on db2
    void *holder2 = AppHybrid_hybrid_init(db2, init_ext, NULL, init_conn2, NULL);
    assert(holder2 != NULL);
    assert(sqlite3_create_function_v2(
        db2, "hybrid_query", 0, SQLITE_UTF8,
        holder2,
        test_hybrid_udf,
        NULL, NULL,
        AppHybrid_hybrid_destructor
    ) == SQLITE_OK);

    sqlite3_stmt *stmt = NULL;

    // Run query on db1 (conn1: 1, ext: 101)
    assert(sqlite3_prepare_v2(db1, "SELECT hybrid_query();", -1, &stmt, NULL) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp((const char*)sqlite3_column_text(stmt, 0), "conn=1,ext=101") == 0);
    sqlite3_finalize(stmt);

    // Run query again on db1 (conn1: 2, ext: 102)
    assert(sqlite3_prepare_v2(db1, "SELECT hybrid_query();", -1, &stmt, NULL) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp((const char*)sqlite3_column_text(stmt, 0), "conn=2,ext=102") == 0);
    sqlite3_finalize(stmt);

    // Run query on db2 (conn2: 1, ext: 103 - shares ext state, isolates conn state!)
    assert(sqlite3_prepare_v2(db2, "SELECT hybrid_query();", -1, &stmt, NULL) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp((const char*)sqlite3_column_text(stmt, 0), "conn=1,ext=103") == 0);
    sqlite3_finalize(stmt);

    // Direct lookup by db handle
    AppHybrid_HybridState db1_state = AppHybrid_hybrid_from_db(db1);
    assert(db1_state.conn->local_query_count == 2);
    assert(db1_state.ext->global_counter == 103);

    AppHybrid_HybridState db2_state = AppHybrid_hybrid_from_db(db2);
    assert(db2_state.conn->local_query_count == 1);
    assert(db2_state.ext->global_counter == 103);

    // Close connections cleanly (triggers hybrid destructors)
    sqlite3_close(db1);
    sqlite3_close(db2);
    remove(db_file);

    printf("test_pure_c_hybrid_state: PASSED\n");
}

int main() {
    printf("==============================================================================\n");
    printf("  RUNNING PURE C DUOSTL & HYBRID STATE TEST SUITE\n");
    printf("==============================================================================\n");

    test_pure_c_hybrid_state();

    printf("\n==============================================================================\n");
    printf("  ALL PURE C HYBRID STATE TESTS PASSED SUCCESSFULLY!\n");
    printf("==============================================================================\n");
    return 0;
}
