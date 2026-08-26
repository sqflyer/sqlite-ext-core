#define SQLITE_CORE
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "sqlite3_udf.hpp"

// ============================================================================
// Shared State Definition
// ============================================================================
struct SharedAppState {
    int counter;
    int accumulator;
    char tag[64];
};

// ============================================================================
// Multiple Functions Sharing the Same State
// ============================================================================

// Function 1: Increment counter (Write access)
static void udf_state_inc(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    SharedAppState* state = SqliteExtState<SharedAppState>::from_context(ctx);
    if (!state) {
        ctx.result_error("SharedAppState not found");
        return;
    }

    int val = 0;
    {
        SqliteExtState<SharedAppState>::WriteGuard lock(state);
        lock->counter++;
        val = lock->counter;
    }
    ctx.result_int(val);
}

// Function 2: Add delta to accumulator (Write access)
static void udf_state_accumulate(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() != 1) {
        ctx.result_error("udf_state_accumulate expects 1 argument");
        return;
    }

    SharedAppState* state = ctx.state<SharedAppState>();
    if (!state) {
        ctx.result_error("SharedAppState not found");
        return;
    }

    int val = 0;
    {
        SqliteExtState<SharedAppState>::WriteGuard lock(state);
        lock->accumulator += static_cast<int>(args[0].as_int64());
        val = lock->accumulator;
    }
    ctx.result_int(val);
}

// Function 3: Set tag string (Write access)
static void udf_state_set_tag(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() != 1) {
        ctx.result_error("udf_state_set_tag expects 1 argument");
        return;
    }

    SharedAppState* state = SqliteExtState<SharedAppState>::from_context(ctx);
    if (!state) {
        ctx.result_error("SharedAppState not found");
        return;
    }

    SqliteStringView str = args[0].as_text();
    {
        SqliteExtState<SharedAppState>::WriteGuard lock(state);
        int copy_len = str.length() < 63 ? static_cast<int>(str.length()) : 63;
        memcpy(lock->tag, str.data(), copy_len);
        lock->tag[copy_len] = '\0';
    }
    ctx.result_text("OK", 2, SQLITE_STATIC);
}

// Function 4: Read aggregated stats combining counter, accumulator, and tag (Read access)
static void udf_state_get_stats(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    SharedAppState* state = ctx.state<SharedAppState>();
    if (!state) {
        ctx.result_error("SharedAppState not found");
        return;
    }

    int c = 0;
    int a = 0;
    char t[64];
    {
        SqliteExtState<SharedAppState>::ReadGuard lock(state);
        c = lock->counter;
        a = lock->accumulator;
        int len = static_cast<int>(strlen(lock->tag));
        if (len > 63) len = 63;
        memcpy(t, lock->tag, len);
        t[len] = '\0';
    }

    // Format stats summary: "c=<counter> a=<acc> tag=<tag>"
    SqliteStringOwned out(ctx.get());
    out.appendall("c=");
    
    char num_buf[32];
    snprintf(num_buf, sizeof(num_buf), "%d", c);
    out.appendall(num_buf);
    
    out.appendall(" a=");
    snprintf(num_buf, sizeof(num_buf), "%d", a);
    out.appendall(num_buf);
    
    out.appendall(" tag=");
    out.appendall(t);
    
    out.result(ctx);
}

// Function 5: Reset state to zero (Write access)
static void udf_state_reset(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    SharedAppState* state = SqliteExtState<SharedAppState>::from_context(ctx);
    if (!state) {
        ctx.result_error("SharedAppState not found");
        return;
    }

    {
        SqliteExtState<SharedAppState>::WriteGuard lock(state);
        lock->counter = 0;
        lock->accumulator = 0;
        lock->tag[0] = '\0';
    }
    ctx.result_int(0);
}

// ============================================================================
// Main Test Runner
// ============================================================================
int main() {
    sqlite3_initialize();

    // ------------------------------------------------------------------------
    // Connection 1: Primary Test Database
    // ------------------------------------------------------------------------
    sqlite3* db1;
    assert(sqlite3_open(":memory:", &db1) == SQLITE_OK);

    printf("1. Initializing custom shared state on db1...\n");
    SqliteExtState<SharedAppState>::get_or_create(db1, [](SharedAppState* s) {
        s->counter = 100;
        s->accumulator = 50;
        const char* initial_tag = "init";
        memcpy(s->tag, initial_tag, 5);
    });

    printf("2. Registering 5 UDFs that share the same SharedAppState...\n");
    assert((SqliteUdf::define_with_state<SharedAppState, udf_state_inc>(db1, "state_inc", 0)) == SQLITE_OK);
    assert((SqliteUdf::define_with_state<SharedAppState, udf_state_accumulate>(db1, "state_accumulate", 1)) == SQLITE_OK);
    assert((SqliteUdf::define_with_state<SharedAppState, udf_state_set_tag>(db1, "state_set_tag", 1)) == SQLITE_OK);
    assert((SqliteUdf::define_with_state<SharedAppState, udf_state_get_stats>(db1, "state_get_stats", 0)) == SQLITE_OK);
    assert((SqliteUdf::define_with_state<SharedAppState, udf_state_reset>(db1, "state_reset", 0)) == SQLITE_OK);

    sqlite3_stmt* stmt;

    printf("3. Testing initial state retrieval via state_get_stats()...\n");
    assert(sqlite3_prepare_v2(db1, "SELECT state_get_stats();", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "c=100 a=50 tag=init") == 0);
    sqlite3_finalize(stmt);

    printf("4. Testing sequential cross-function state mutation...\n");
    // Function 1 mutates counter: 100 -> 101 -> 102 -> 103
    assert(sqlite3_prepare_v2(db1, "SELECT state_inc(), state_inc(), state_inc();", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 101);
    assert(sqlite3_column_int(stmt, 1) == 102);
    assert(sqlite3_column_int(stmt, 2) == 103);
    sqlite3_finalize(stmt);

    // Function 2 mutates accumulator: 50 + 20 + 30 = 100
    assert(sqlite3_prepare_v2(db1, "SELECT state_accumulate(20), state_accumulate(30);", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 70);
    assert(sqlite3_column_int(stmt, 1) == 100);
    sqlite3_finalize(stmt);

    // Function 3 mutates tag: "init" -> "active_v2"
    assert(sqlite3_prepare_v2(db1, "SELECT state_set_tag('active_v2');", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "OK") == 0);
    sqlite3_finalize(stmt);

    // Function 4 reads all updated values: c=103 a=100 tag=active_v2
    assert(sqlite3_prepare_v2(db1, "SELECT state_get_stats();", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "c=103 a=100 tag=active_v2") == 0);
    sqlite3_finalize(stmt);

    printf("5. Testing stateful UDFs across table row iterations...\n");
    assert(sqlite3_exec(db1, "CREATE TABLE delta_log(delta INT);", nullptr, nullptr, nullptr) == SQLITE_OK);
    assert(sqlite3_exec(db1, "INSERT INTO delta_log VALUES (5), (10), (15);", nullptr, nullptr, nullptr) == SQLITE_OK);

    // Run accumulator across each row: 100 + 5 = 105, 105 + 10 = 115, 115 + 15 = 130
    assert(sqlite3_prepare_v2(db1, "SELECT state_accumulate(delta) FROM delta_log;", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 105);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 115);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 130);
    assert(sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);

    // Verify counter was untouched (still 103) while accumulator changed to 130
    assert(sqlite3_prepare_v2(db1, "SELECT state_get_stats();", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "c=103 a=130 tag=active_v2") == 0);
    sqlite3_finalize(stmt);

    // ------------------------------------------------------------------------
    // Connection 2: Per-Connection Database Isolation Test
    // ------------------------------------------------------------------------
    printf("6. Testing per-connection database state isolation (db2 vs db1)...\n");
    sqlite3* db2;
    assert(sqlite3_open(":memory:", &db2) == SQLITE_OK);

    // Initialize db2 with different starting state
    SqliteExtState<SharedAppState>::get_or_create(db2, [](SharedAppState* s) {
        s->counter = 0;
        s->accumulator = 0;
        const char* default_tag = "db2_fresh";
        memcpy(s->tag, default_tag, 10);
    });

    assert((SqliteUdf::define_with_state<SharedAppState, udf_state_inc>(db2, "state_inc", 0)) == SQLITE_OK);
    assert((SqliteUdf::define_with_state<SharedAppState, udf_state_accumulate>(db2, "state_accumulate", 1)) == SQLITE_OK);
    assert((SqliteUdf::define_with_state<SharedAppState, udf_state_get_stats>(db2, "state_get_stats", 0)) == SQLITE_OK);

    // Check db2 stats
    assert(sqlite3_prepare_v2(db2, "SELECT state_get_stats();", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "c=0 a=0 tag=db2_fresh") == 0);
    sqlite3_finalize(stmt);

    // Mutate db2
    assert(sqlite3_prepare_v2(db2, "SELECT state_inc(), state_accumulate(999);", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 1);
    assert(sqlite3_column_int(stmt, 1) == 999);
    sqlite3_finalize(stmt);

    // Verify db1 state remains completely untouched by db2's mutations
    assert(sqlite3_prepare_v2(db1, "SELECT state_get_stats();", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "c=103 a=130 tag=active_v2") == 0);
    sqlite3_finalize(stmt);

    // ------------------------------------------------------------------------
    // Reset Test on db1
    // ------------------------------------------------------------------------
    printf("7. Testing state_reset() on db1...\n");
    assert(sqlite3_prepare_v2(db1, "SELECT state_reset();", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 0);
    sqlite3_finalize(stmt);

    assert(sqlite3_prepare_v2(db1, "SELECT state_get_stats();", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "c=0 a=0 tag=") == 0);
    sqlite3_finalize(stmt);

    sqlite3_close(db2);
    sqlite3_close(db1);
    sqlite3_shutdown();

    printf("\nAll 7 Shared State UDF Test Suites Passed with Complete Coverage!\n");
    return 0;
}
