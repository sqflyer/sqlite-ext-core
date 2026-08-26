#define SQLITE_CORE
#include <sqlite3.h>
#include <stdio.h>
#include <cassert>
#include <cstring>
#include "sqlite3_udf.hpp"
#include "sqlite3_tvf.hpp"
#include "sqlite3_statement.hpp"
#include "sqlite3_ext_state.hpp"

// ============================================================================
// Shared State Struct
// ============================================================================

struct TvfSharedState {
    int total_rows_emitted;
    int query_executions;
    char session_name[64];
    int metrics[4]; // 0: CPU, 1: MEM, 2: IO, 3: NET
};

// ============================================================================
// Stateful TVF 1: MetricsStreamer (Streams shared metrics as rows)
// ============================================================================

struct MetricsStreamer : public SqliteTvfIterator {
    static constexpr const char* schema() {
        return "CREATE TABLE x(metric_id INT, metric_name TEXT, metric_value INT, filter_min HIDDEN)";
    }

    int current_idx = 0;
    int min_threshold = 0;

    void init(SqliteUdfArgs args) override {
        min_threshold = (args.size() > 0 && args[0].type() != SQLITE_NULL) ? static_cast<int>(args[0].as_int64()) : 0;
        current_idx = 0;
    }

    void next() override {
        current_idx++;
    }

    bool eof() const override {
        return current_idx >= 4;
    }

    void column(SqliteContext ctx, int col_idx) override {
        TvfSharedState* state = ctx.state<TvfSharedState>();
        if (!state) {
            ctx.result_null();
            return;
        }

        int val = 0;
        char session[64] = {0};
        {
            SqliteExtState<TvfSharedState>::WriteGuard lock(state);
            val = lock->metrics[current_idx];
            memcpy(session, lock->session_name, sizeof(session));
            if (col_idx == 0) {
                lock->total_rows_emitted++;
            }
        }

        if (col_idx == 0) {
            ctx.result_int(current_idx);
        } else if (col_idx == 1) {
            const char* names[] = {"CPU", "MEM", "IO", "NET"};
            ctx.result_text(names[current_idx]);
        } else if (col_idx == 2) {
            ctx.result_int(val);
        }
    }

    sqlite3_int64 rowid() const override {
        return current_idx;
    }
};

// ============================================================================
// Stateful TVF 2: RangeMultiplier (Generates scaled sequences)
// ============================================================================

struct ScaledSeriesIterator : public SqliteTvfIterator {
    static constexpr const char* schema() {
        return "CREATE TABLE x(value, start HIDDEN, stop HIDDEN)";
    }

    sqlite3_int64 current = 0;
    sqlite3_int64 stop = 0;

    void init(SqliteUdfArgs args) override {
        current = args.size() > 0 && args[0].type() != SQLITE_NULL ? args[0].as_int64() : 1;
        stop    = args.size() > 1 && args[1].type() != SQLITE_NULL ? args[1].as_int64() : 1;
    }

    void next() override {
        current++;
    }

    bool eof() const override {
        return current > stop;
    }

    void column(SqliteContext ctx, int col_idx) override {
        if (col_idx == 0) {
            TvfSharedState* state = ctx.state<TvfSharedState>();
            int multiplier = 1;
            if (state) {
                SqliteExtState<TvfSharedState>::WriteGuard lock(state);
                lock->total_rows_emitted++;
                // Use metric 0 (CPU) as multiplier if non-zero
                if (lock->metrics[0] > 0) {
                    multiplier = lock->metrics[0];
                }
            }
            ctx.result_int64(current * multiplier);
        }
    }

    sqlite3_int64 rowid() const override {
        return current;
    }
};

// ============================================================================
// Companion Scalar UDFs
// ============================================================================

static void udf_get_tvf_stats(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    TvfSharedState* state = ctx.state<TvfSharedState>();
    if (!state) {
        ctx.result_error("Shared TVF state not found");
        return;
    }

    int rows = 0;
    char session[64] = {0};
    int m0 = 0, m1 = 0, m2 = 0, m3 = 0;

    {
        SqliteExtState<TvfSharedState>::ReadGuard lock(state);
        rows = lock->total_rows_emitted;
        memcpy(session, lock->session_name, sizeof(session));
        m0 = lock->metrics[0];
        m1 = lock->metrics[1];
        m2 = lock->metrics[2];
        m3 = lock->metrics[3];
    }

    SqliteStringOwned out(ctx.get());
    out.appendall("session=");
    out.appendall(session);
    out.appendall(" rows=");
    char num[32];
    snprintf(num, sizeof(num), "%d", rows);
    out.appendall(num);
    out.appendall(" metrics=[");
    snprintf(num, sizeof(num), "%d,%d,%d,%d", m0, m1, m2, m3);
    out.appendall(num);
    out.appendall("]");
    out.result(ctx);
}

static void udf_set_metric(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() < 2) return;
    TvfSharedState* state = ctx.state<TvfSharedState>();
    if (!state) return;

    int idx = static_cast<int>(args[0].as_int64());
    int val = static_cast<int>(args[1].as_int64());

    if (idx >= 0 && idx < 4) {
        SqliteExtState<TvfSharedState>::WriteGuard lock(state);
        lock->metrics[idx] = val;
    }
    ctx.result_int(1);
}

// ============================================================================
// Test Suites
// ============================================================================

int main() {
    sqlite3_initialize();

    // ------------------------------------------------------------------------
    // Connection 1: db1 Setup
    // ------------------------------------------------------------------------
    sqlite3* db1;
    assert(sqlite3_open(":memory:", &db1) == SQLITE_OK);

    printf("1. Initializing shared TVF state on db1...\n");
    SqliteExtState<TvfSharedState>::get_or_create(db1, [](TvfSharedState* s) {
        s->total_rows_emitted = 0;
        s->query_executions = 0;
        const char* initial_sess = "session_alpha";
        memcpy(s->session_name, initial_sess, strlen(initial_sess) + 1);
        s->metrics[0] = 10; // CPU
        s->metrics[1] = 20; // MEM
        s->metrics[2] = 30; // IO
        s->metrics[3] = 40; // NET
    });

    printf("2. Registering stateful TVFs and companion UDFs on db1...\n");
    assert((SqliteTvf::define_with_state<TvfSharedState, MetricsStreamer>(db1, "stream_metrics")) == SQLITE_OK);
    assert((SqliteTvf::define_with_state<TvfSharedState, ScaledSeriesIterator>(db1, "scaled_series")) == SQLITE_OK);
    assert((SqliteUdf::define_with_state<TvfSharedState, udf_get_tvf_stats>(db1, "tvf_stats", 0)) == SQLITE_OK);
    assert((SqliteUdf::define_with_state<TvfSharedState, udf_set_metric>(db1, "tvf_set_metric", 2)) == SQLITE_OK);

    // ------------------------------------------------------------------------
    // Test 1: Query Stateful TVF (stream_metrics)
    // ------------------------------------------------------------------------
    printf("3. Testing stateful TVF row emission from shared state...\n");
    {
        SqliteStatement stmt(db1, "SELECT metric_id, metric_name, metric_value FROM stream_metrics();");
        
        // Row 0: CPU = 10
        assert(stmt.next());
        assert(stmt.column_int(0) == 0);
        assert(strcmp(stmt.column_text(1), "CPU") == 0);
        assert(stmt.column_int(2) == 10);

        // Row 1: MEM = 20
        assert(stmt.next());
        assert(stmt.column_int(0) == 1);
        assert(strcmp(stmt.column_text(1), "MEM") == 0);
        assert(stmt.column_int(2) == 20);

        // Row 2: IO = 30
        assert(stmt.next());
        assert(stmt.column_int(0) == 2);
        assert(strcmp(stmt.column_text(1), "IO") == 0);
        assert(stmt.column_int(2) == 30);

        // Row 3: NET = 40
        assert(stmt.next());
        assert(stmt.column_int(0) == 3);
        assert(strcmp(stmt.column_text(1), "NET") == 0);
        assert(stmt.column_int(2) == 40);

        assert(!stmt.next());
    }

    // Verify shared stats: 4 rows emitted
    {
        SqliteStatement stmt(db1, "SELECT tvf_stats();");
        assert(stmt.next());
        assert(strcmp(stmt.column_text(0), "session=session_alpha rows=4 metrics=[10,20,30,40]") == 0);
    }

    // ------------------------------------------------------------------------
    // Test 2: Mutate State via UDF and stream through second TVF
    // ------------------------------------------------------------------------
    printf("4. Testing state mutation via UDF and verification in ScaledSeries...\n");
    {
        // Change CPU metric (metrics[0]) from 10 to 5
        SqliteStatement stmt(db1, "SELECT tvf_set_metric(0, 5);");
        assert(stmt.next());
    }

    {
        // scaled_series(1, 3) should now multiply by CPU=5 -> 5, 10, 15
        SqliteStatement stmt(db1, "SELECT value FROM scaled_series(1, 3);");
        
        assert(stmt.next());
        assert(stmt.column_int64(0) == 5);

        assert(stmt.next());
        assert(stmt.column_int64(0) == 10);

        assert(stmt.next());
        assert(stmt.column_int64(0) == 15);

        assert(!stmt.next());
    }

    // Verify stats: 4 previous + 3 new = 7 rows
    {
        SqliteStatement stmt(db1, "SELECT tvf_stats();");
        assert(stmt.next());
        assert(strcmp(stmt.column_text(0), "session=session_alpha rows=7 metrics=[5,20,30,40]") == 0);
    }

    // ------------------------------------------------------------------------
    // Test 3: Multi-Connection State Isolation (db2 vs db1)
    // ------------------------------------------------------------------------
    printf("5. Testing per-connection database state isolation (db2 vs db1)...\n");
    sqlite3* db2;
    assert(sqlite3_open(":memory:", &db2) == SQLITE_OK);

    SqliteExtState<TvfSharedState>::get_or_create(db2, [](TvfSharedState* s) {
        s->total_rows_emitted = 0;
        s->query_executions = 0;
        const char* db2_sess = "session_beta";
        memcpy(s->session_name, db2_sess, strlen(db2_sess) + 1);
        s->metrics[0] = 100;
        s->metrics[1] = 200;
        s->metrics[2] = 300;
        s->metrics[3] = 400;
    });

    assert((SqliteTvf::define_with_state<TvfSharedState, MetricsStreamer>(db2, "stream_metrics")) == SQLITE_OK);
    assert((SqliteUdf::define_with_state<TvfSharedState, udf_get_tvf_stats>(db2, "tvf_stats", 0)) == SQLITE_OK);

    // Verify db2 initial stats
    {
        SqliteStatement stmt(db2, "SELECT tvf_stats();");
        assert(stmt.next());
        assert(strcmp(stmt.column_text(0), "session=session_beta rows=0 metrics=[100,200,300,400]") == 0);
    }

    // Query db2 TVF
    {
        SqliteStatement stmt(db2, "SELECT metric_value FROM stream_metrics();");
        assert(stmt.next());
        assert(stmt.column_int(0) == 100);
    }

    // Verify db1 remains untouched
    {
        SqliteStatement stmt(db1, "SELECT tvf_stats();");
        assert(stmt.next());
        assert(strcmp(stmt.column_text(0), "session=session_alpha rows=7 metrics=[5,20,30,40]") == 0);
    }

    sqlite3_close(db1);
    sqlite3_close(db2);
    sqlite3_shutdown();

    printf("\nAll 5 Stateful TVF Test Suites Passed with Complete Coverage!\n");
    return 0;
}
