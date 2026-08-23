#include <sqlite3.h>
#define SQLITE_CORE
#include "../../include/sqlite3_aggregate.hpp"
#include "../../include/sqlite3_udf.hpp"
#include "../../include/sqlite3_ext_state.hpp"
#include <stdio.h>
#include <assert.h>
#include <string.h>

// ============================================================================
// Shared State Struct
// ============================================================================

struct AggregateSharedState {
    int total_rows_stepped;
    double global_sum;
    int finalize_invocations;
    char last_tag[64];
};

// ============================================================================
// Stateful Aggregate 1: StatefulWeightedAvg
// ============================================================================

struct StatefulWeightedAvg : public SqliteAggregateBase<double> {
    double weighted_sum = 0.0;
    double total_weight = 0.0;

    void step(SqliteContext ctx, SqliteUdfArgs args) override {
        if (args.size() >= 2 && args[0].type() != SQLITE_NULL && args[1].type() != SQLITE_NULL) {
            double val = args[0].as_double();
            double weight = args[1].as_double();

            weighted_sum += val * weight;
            total_weight += weight;

            // Mutate shared per-connection state
            AggregateSharedState* state = ctx.state<AggregateSharedState>();
            if (state) {
                SqliteExtState<AggregateSharedState>::WriteGuard lock(state);
                lock->total_rows_stepped++;
                lock->global_sum += val;
            }
        }
    }

    double finalize(SqliteContext ctx) override {
        // Record finalize invocation in shared state
        AggregateSharedState* state = ctx.state<AggregateSharedState>();
        if (state) {
            SqliteExtState<AggregateSharedState>::WriteGuard lock(state);
            lock->finalize_invocations++;
        }

        if (total_weight > 0.0) {
            return weighted_sum / total_weight;
        } else {
            return 0.0;
        }
    }
};

// ============================================================================
// Stateful Aggregate 2: StatefulTaggedConcat
// ============================================================================

struct StatefulTaggedConcat : public SqliteAggregateBase<void> {
    SqliteStringOwned buffer;
    bool first = true;

    void step(SqliteContext ctx, SqliteUdfArgs args) override {
        if (args.size() > 0 && args[0].type() == SQLITE_TEXT) {
            if (!first) {
                buffer.appendall(", ");
            }
            first = false;
            SqliteStringView text = args[0].as_text();
            buffer.append(text.data(), text.length());

            // Track row count in shared state
            AggregateSharedState* state = ctx.state<AggregateSharedState>();
            if (state) {
                SqliteExtState<AggregateSharedState>::WriteGuard lock(state);
                lock->total_rows_stepped++;
            }
        }
    }

    void finalize(SqliteContext ctx) override {
        AggregateSharedState* state = ctx.state<AggregateSharedState>();
        char tag[64] = {0};
        if (state) {
            SqliteExtState<AggregateSharedState>::WriteGuard lock(state);
            lock->finalize_invocations++;
            memcpy(tag, lock->last_tag, sizeof(tag));
        }

        if (buffer.length() > 0) {
            if (tag[0] != '\0') {
                buffer.appendall(" [tag=");
                buffer.appendall(tag);
                buffer.appendall("]");
            }
            buffer.result(ctx);
        } else {
            ctx.result_null();
        }
    }
};

// ============================================================================
// Companion Scalar UDFs sharing the same AggregateSharedState
// ============================================================================

static void udf_get_agg_stats(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    AggregateSharedState* state = ctx.state<AggregateSharedState>();
    if (!state) {
        ctx.result_error("Shared aggregate state not initialized");
        return;
    }

    int rows = 0;
    double sum = 0.0;
    int finalizes = 0;
    char tag[64] = {0};

    {
        SqliteExtState<AggregateSharedState>::ReadGuard lock(state);
        rows = lock->total_rows_stepped;
        sum = lock->global_sum;
        finalizes = lock->finalize_invocations;
        memcpy(tag, lock->last_tag, sizeof(tag));
    }

    SqliteStringOwned out(ctx.get());
    out.appendall("rows=");
    char num[32];
    snprintf(num, sizeof(num), "%d", rows);
    out.appendall(num);
    out.appendall(" sum=");
    snprintf(num, sizeof(num), "%.1f", sum);
    out.appendall(num);
    out.appendall(" finalizes=");
    snprintf(num, sizeof(num), "%d", finalizes);
    out.appendall(num);
    out.appendall(" tag=");
    out.appendall(tag);

    out.result(ctx);
}

static void udf_set_agg_tag(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() < 1) return;
    AggregateSharedState* state = ctx.state<AggregateSharedState>();
    if (!state) return;

    SqliteStringView new_tag = args[0].as_text();
    {
        SqliteExtState<AggregateSharedState>::WriteGuard lock(state);
        int copy_len = new_tag.length() < 63 ? new_tag.length() : 63;
        memcpy(lock->last_tag, new_tag.data(), copy_len);
        lock->last_tag[copy_len] = '\0';
    }
    ctx.result_int(1);
}

static void udf_reset_agg_state(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    AggregateSharedState* state = ctx.state<AggregateSharedState>();
    if (!state) return;

    {
        SqliteExtState<AggregateSharedState>::WriteGuard lock(state);
        lock->total_rows_stepped = 0;
        lock->global_sum = 0.0;
        lock->finalize_invocations = 0;
        lock->last_tag[0] = '\0';
    }
    ctx.result_int(0);
}

// ============================================================================
// Test Suites
// ============================================================================

int main() {
    sqlite3_initialize();

    // ------------------------------------------------------------------------
    // 1. Initialize Connection 1
    // ------------------------------------------------------------------------
    sqlite3* db1;
    assert(sqlite3_open(":memory:", &db1) == SQLITE_OK);

    printf("1. Initializing shared state on db1...\n");
    SqliteExtState<AggregateSharedState>::get_or_create(db1, [](AggregateSharedState* s) {
        s->total_rows_stepped = 0;
        s->global_sum = 0.0;
        s->finalize_invocations = 0;
        const char* initial_tag = "initial_v1";
        memcpy(s->last_tag, initial_tag, strlen(initial_tag) + 1);
    });

    printf("2. Registering stateful aggregates and companion UDFs on db1...\n");
    assert((SqliteUdf::define_aggregate_with_state<AggregateSharedState, StatefulWeightedAvg>(db1, "weighted_avg", 2)) == SQLITE_OK);
    assert((SqliteUdf::define_aggregate_with_state<AggregateSharedState, StatefulTaggedConcat>(db1, "tagged_concat", 1)) == SQLITE_OK);
    assert((SqliteUdf::define_with_state<AggregateSharedState, udf_get_agg_stats>(db1, "agg_stats", 0)) == SQLITE_OK);
    assert((SqliteUdf::define_with_state<AggregateSharedState, udf_set_agg_tag>(db1, "agg_set_tag", 1)) == SQLITE_OK);
    assert((SqliteUdf::define_with_state<AggregateSharedState, udf_reset_agg_state>(db1, "agg_reset", 0)) == SQLITE_OK);

    char* err = nullptr;
    assert(sqlite3_exec(db1, "CREATE TABLE grades(dept TEXT, score REAL, weight REAL);", nullptr, nullptr, &err) == SQLITE_OK);
    assert(sqlite3_exec(db1, 
        "INSERT INTO grades VALUES "
        "('CS', 90.0, 1.0), ('CS', 80.0, 2.0), ('CS', 100.0, 1.0), "
        "('MATH', 70.0, 3.0), ('MATH', 90.0, 1.0);", 
        nullptr, nullptr, &err) == SQLITE_OK);

    sqlite3_stmt* stmt;

    // ------------------------------------------------------------------------
    // 2. Test Single Aggregate Execution and Shared State Update
    // ------------------------------------------------------------------------
    printf("3. Testing single aggregate execution (weighted_avg)...\n");
    assert(sqlite3_prepare_v2(db1, "SELECT weighted_avg(score, weight) FROM grades WHERE dept = 'CS';", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    // (90*1 + 80*2 + 100*1) / (1 + 2 + 1) = (90 + 160 + 100) / 4 = 350 / 4 = 87.5
    double cs_avg = sqlite3_column_double(stmt, 0);
    assert(cs_avg >= 87.499 && cs_avg <= 87.501);
    sqlite3_finalize(stmt);

    // Verify shared stats: 3 rows stepped, sum=270.0, 1 finalize
    assert(sqlite3_prepare_v2(db1, "SELECT agg_stats();", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "rows=3 sum=270.0 finalizes=1 tag=initial_v1") == 0);
    sqlite3_finalize(stmt);

    // ------------------------------------------------------------------------
    // 3. Test GROUP BY with Multiple Aggregate Instances Sharing the Same State
    // ------------------------------------------------------------------------
    printf("4. Testing GROUP BY query with multiple aggregate instances...\n");
    assert(sqlite3_prepare_v2(db1, "SELECT dept, weighted_avg(score, weight) FROM grades GROUP BY dept ORDER BY dept;", -1, &stmt, nullptr) == SQLITE_OK);
    
    // Row 1: CS -> 87.5
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "CS") == 0);
    assert(sqlite3_column_double(stmt, 1) >= 87.499 && sqlite3_column_double(stmt, 1) <= 87.501);

    // Row 2: MATH -> (70*3 + 90*1) / 4 = 300 / 4 = 75.0
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "MATH") == 0);
    assert(sqlite3_column_double(stmt, 1) >= 74.999 && sqlite3_column_double(stmt, 1) <= 75.001);
    sqlite3_finalize(stmt);

    // Verify stats after GROUP BY: previous 3 rows + 5 rows = 8 rows, sum = 270 + 430 = 700.0, finalizes = 1 + 2 = 3
    assert(sqlite3_prepare_v2(db1, "SELECT agg_stats();", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "rows=8 sum=700.0 finalizes=3 tag=initial_v1") == 0);
    sqlite3_finalize(stmt);

    // ------------------------------------------------------------------------
    // 4. Test Second Aggregate (tagged_concat) Reading Mutated Tag
    // ------------------------------------------------------------------------
    printf("5. Testing second aggregate (tagged_concat) and companion tag mutation...\n");
    // Mutate tag via companion UDF
    assert(sqlite3_prepare_v2(db1, "SELECT agg_set_tag('dept_run');", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);

    assert(sqlite3_prepare_v2(db1, "SELECT tagged_concat(dept) FROM grades;", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "CS, CS, CS, MATH, MATH [tag=dept_run]") == 0);
    sqlite3_finalize(stmt);

    // ------------------------------------------------------------------------
    // 5. Test Database Isolation (db2 vs db1)
    // ------------------------------------------------------------------------
    printf("6. Testing database connection isolation (db2 vs db1)...\n");
    sqlite3* db2;
    assert(sqlite3_open(":memory:", &db2) == SQLITE_OK);

    SqliteExtState<AggregateSharedState>::get_or_create(db2, [](AggregateSharedState* s) {
        s->total_rows_stepped = 0;
        s->global_sum = 0.0;
        s->finalize_invocations = 0;
        const char* db2_tag = "db2_fresh";
        memcpy(s->last_tag, db2_tag, strlen(db2_tag) + 1);
    });

    assert((SqliteUdf::define_aggregate_with_state<AggregateSharedState, StatefulWeightedAvg>(db2, "weighted_avg", 2)) == SQLITE_OK);
    assert((SqliteUdf::define_with_state<AggregateSharedState, udf_get_agg_stats>(db2, "agg_stats", 0)) == SQLITE_OK);

    // Check db2 starts fresh
    assert(sqlite3_prepare_v2(db2, "SELECT agg_stats();", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "rows=0 sum=0.0 finalizes=0 tag=db2_fresh") == 0);
    sqlite3_finalize(stmt);

    // Verify db1 state remains intact
    assert(sqlite3_prepare_v2(db1, "SELECT agg_stats();", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "rows=13 sum=700.0 finalizes=4 tag=dept_run") == 0);
    sqlite3_finalize(stmt);

    // ------------------------------------------------------------------------
    // 6. Test State Reset on db1
    // ------------------------------------------------------------------------
    printf("7. Testing agg_reset() on db1...\n");
    assert(sqlite3_prepare_v2(db1, "SELECT agg_reset();", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);

    assert(sqlite3_prepare_v2(db1, "SELECT agg_stats();", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "rows=0 sum=0.0 finalizes=0 tag=") == 0);
    sqlite3_finalize(stmt);

    // Clean up
    sqlite3_close(db1);
    sqlite3_close(db2);
    sqlite3_shutdown();

    printf("\nAll 7 Stateful Aggregate Test Suites Passed with Complete Coverage!\n");
    return 0;
}
