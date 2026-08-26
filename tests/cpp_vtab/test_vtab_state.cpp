#define SQLITE_CORE
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sqlite3.h>
#include "../../include/sqlite3_vtab.hpp"
#include "../../include/sqlite3_udf.hpp"
#include "../../include/sqlite3_ext_state.hpp"

// ============================================================================
// 1. Shared State Struct
// ============================================================================

struct VTabSharedState {
    int total_rows_read;
    int total_rows_inserted;
    int query_count;
    char session_tag[64];
    int cache[5]; // Stores 5 slot values
};

// ============================================================================
// 2. Stateful Virtual Table Cursor (Reads from VTabSharedState)
// ============================================================================

class StateCacheCursor : public SqliteVTabCursor {
private:
    int m_current_slot;
    int m_max_slot;
    VTabSharedState* m_bound_state;

public:
    StateCacheCursor(VTabSharedState* state) 
        : m_current_slot(0), m_max_slot(0), m_bound_state(state) {}

    int filter(int idxNum, const char* idxStr, SqliteUdfArgs args) override {
        (void)idxNum; (void)idxStr; (void)args;
        m_current_slot = 0;
        m_max_slot = 5;
        if (m_bound_state) {
            SqliteExtState<VTabSharedState>::WriteGuard lock(m_bound_state);
            lock->query_count++;
        }
        return SQLITE_OK;
    }

    int next() override {
        m_current_slot++;
        return SQLITE_OK;
    }

    bool eof() override {
        return m_current_slot >= m_max_slot;
    }

    int column(SqliteContext& ctx, int N) override {
        VTabSharedState* state = ctx.state<VTabSharedState>();
        if (!state) {
            ctx.result_null();
            return SQLITE_OK;
        }

        int val = 0;
        char tag[64] = {0};
        {
            SqliteExtState<VTabSharedState>::WriteGuard lock(state);
            val = lock->cache[m_current_slot];
            memcpy(tag, lock->session_tag, sizeof(tag));
            if (N == 0) {
                lock->total_rows_read++;
            }
        }

        if (N == 0) {
            ctx.result_int(m_current_slot); // Slot ID
        } else if (N == 1) {
            ctx.result_int(val);            // Cached Value
        } else if (N == 2) {
            ctx.result_text(tag);           // Session Tag
        }
        return SQLITE_OK;
    }

    int rowid(sqlite3_int64& pRowid) override {
        pRowid = m_current_slot;
        return SQLITE_OK;
    }
};

// ============================================================================
// 3. Stateful Virtual Table: Writable Cache Table
// ============================================================================

class StateCacheTable : public SqliteVTable {
private:
    VTabSharedState* m_bound_state;

public:
    StateCacheTable(sqlite3* db, VTabSharedState* state) 
        : SqliteVTable(db), m_bound_state(state) {}

    static int connect(SqliteConnectArgs& args) {
        int rc = sqlite3_declare_vtab(args.db(), "CREATE TABLE x(slot_id INT, val INT, session TEXT)");
        if (rc == SQLITE_OK) {
            VTabSharedState* state = args.state<VTabSharedState>();
            args.set_instance(sqlite_new<StateCacheTable>(args.db(), state));
        }
        return rc;
    }

    int bestIndex(SqliteIndexInfo& info) override {
        info.set_estimated_cost(10.0);
        return SQLITE_OK;
    }

    SqliteVTabCursor* open() override {
        return sqlite_new<StateCacheCursor>(m_bound_state);
    }

    int update(SqliteUdfArgs args, sqlite3_int64* pRowid) override {
        if (!m_bound_state) return SQLITE_ERROR;

        // Handling INSERT: args[0] is NULL, args[1] is new rowid, args[2..N] are column values
        if (args[0].type() == SQLITE_NULL && args.size() >= 4) {
            int slot = static_cast<int>(args[2].as_int64());
            int val = static_cast<int>(args[3].as_int64());

            if (slot >= 0 && slot < 5) {
                SqliteExtState<VTabSharedState>::WriteGuard lock(m_bound_state);
                lock->cache[slot] = val;
                lock->total_rows_inserted++;
                if (pRowid) *pRowid = slot;
                return SQLITE_OK;
            }
        }
        return SQLITE_ERROR;
    }
};

// ============================================================================
// 4. Companion Scalar UDFs for Cross-Subsystem Mutation
// ============================================================================

static void udf_vtab_get_stats(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    VTabSharedState* state = ctx.state<VTabSharedState>();
    if (!state) {
        ctx.result_error("Shared VTab state not found");
        return;
    }

    int reads = 0, inserts = 0, queries = 0;
    char tag[64] = {0};
    int c[5] = {0};

    {
        SqliteExtState<VTabSharedState>::ReadGuard lock(state);
        reads = lock->total_rows_read;
        inserts = lock->total_rows_inserted;
        queries = lock->query_count;
        memcpy(tag, lock->session_tag, sizeof(tag));
        for (int i = 0; i < 5; ++i) c[i] = lock->cache[i];
    }

    SqliteStringOwned out(ctx.get());
    out.appendall("tag=");
    out.appendall(tag);
    out.appendall(" reads=");
    char num[32];
    snprintf(num, sizeof(num), "%d", reads);
    out.appendall(num);
    out.appendall(" inserts=");
    snprintf(num, sizeof(num), "%d", inserts);
    out.appendall(num);
    out.appendall(" queries=");
    snprintf(num, sizeof(num), "%d", queries);
    out.appendall(num);
    out.appendall(" cache=[");
    snprintf(num, sizeof(num), "%d,%d,%d,%d,%d", c[0], c[1], c[2], c[3], c[4]);
    out.appendall(num);
    out.appendall("]");
    out.result(ctx);
}

static void udf_vtab_set_tag(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() < 1 || args[0].type() != SQLITE_TEXT) return;
    VTabSharedState* state = ctx.state<VTabSharedState>();
    if (!state) return;

    SqliteStringView tag = args[0].as_text();
    {
        SqliteExtState<VTabSharedState>::WriteGuard lock(state);
        int len = tag.length() < 63 ? tag.length() : 63;
        memcpy(lock->session_tag, tag.data(), len);
        lock->session_tag[len] = '\0';
    }
    ctx.result_int(1);
}

// ============================================================================
// 5. Test Suite Execution
// ============================================================================

void run_stateful_vtab_tests() {
    sqlite3_initialize();

    // ------------------------------------------------------------------------
    // Database 1 Setup
    // ------------------------------------------------------------------------
    sqlite3* db1 = nullptr;
    assert(sqlite3_open(":memory:", &db1) == SQLITE_OK);

    printf("1. Initializing shared state on db1...\n");
    SqliteExtState<VTabSharedState>::get_or_create(db1, [](VTabSharedState* s) {
        s->total_rows_read = 0;
        s->total_rows_inserted = 0;
        s->query_count = 0;
        const char* initial_tag = "NODE_PRIMARY";
        memcpy(s->session_tag, initial_tag, strlen(initial_tag) + 1);
        s->cache[0] = 100;
        s->cache[1] = 200;
        s->cache[2] = 300;
        s->cache[3] = 400;
        s->cache[4] = 500;
    });

    printf("2. Registering stateful virtual table and companion UDFs on db1...\n");
    assert((SqliteVTab::define_with_state<VTabSharedState, StateCacheTable, VTabOptions::Writable>(db1, "state_cache")) == SQLITE_OK);
    assert((SqliteUdf::define_with_state<VTabSharedState, udf_vtab_get_stats>(db1, "vtab_stats", 0)) == SQLITE_OK);
    assert((SqliteUdf::define_with_state<VTabSharedState, udf_vtab_set_tag>(db1, "vtab_set_tag", 1)) == SQLITE_OK);

    assert(sqlite3_exec(db1, "CREATE VIRTUAL TABLE my_cache USING state_cache();", nullptr, nullptr, nullptr) == SQLITE_OK);

    // ------------------------------------------------------------------------
    // Test 1: Query Virtual Table rows from shared state
    // ------------------------------------------------------------------------
    printf("3. Testing virtual table streaming from shared state...\n");
    {
        SqliteStatement stmt(db1, "SELECT slot_id, val, session FROM my_cache;");
        
        int expected_vals[] = {100, 200, 300, 400, 500};
        int row_count = 0;
        while (stmt.next()) {
            assert(stmt.column_int(0) == row_count);
            assert(stmt.column_int(1) == expected_vals[row_count]);
            assert(strcmp(stmt.column_text(2), "NODE_PRIMARY") == 0);
            row_count++;
        }
        assert(row_count == 5);
    }

    // Verify stats: 5 rows read, 1 query
    {
        SqliteStatement stmt(db1, "SELECT vtab_stats();");
        assert(stmt.next());
        assert(strcmp(stmt.column_text(0), "tag=NODE_PRIMARY reads=5 inserts=0 queries=1 cache=[100,200,300,400,500]") == 0);
    }

    // ------------------------------------------------------------------------
    // Test 2: Mutate Shared State via Virtual Table INSERT
    // ------------------------------------------------------------------------
    printf("4. Testing virtual table INSERT mutating shared state...\n");
    {
        assert(sqlite3_exec(db1, "INSERT INTO my_cache(slot_id, val) VALUES (2, 999);", nullptr, nullptr, nullptr) == SQLITE_OK);
        assert(sqlite3_exec(db1, "INSERT INTO my_cache(slot_id, val) VALUES (4, 777);", nullptr, nullptr, nullptr) == SQLITE_OK);
    }

    // Verify stats: 2 inserts reflected in shared cache
    {
        SqliteStatement stmt(db1, "SELECT vtab_stats();");
        assert(stmt.next());
        assert(strcmp(stmt.column_text(0), "tag=NODE_PRIMARY reads=5 inserts=2 queries=1 cache=[100,200,999,400,777]") == 0);
    }

    // ------------------------------------------------------------------------
    // Test 3: Mutate State via UDF and verify in Virtual Table SELECT
    // ------------------------------------------------------------------------
    printf("5. Testing companion UDF state mutation and reflection in Virtual Table...\n");
    {
        SqliteStatement stmt(db1, "SELECT vtab_set_tag('NODE_FAILOVER');");
        assert(stmt.next());
    }

    {
        SqliteStatement stmt(db1, "SELECT val, session FROM my_cache WHERE slot_id = 2;");
        assert(stmt.next());
        assert(stmt.column_int(0) == 999);
        assert(strcmp(stmt.column_text(1), "NODE_FAILOVER") == 0);
    }

    // ------------------------------------------------------------------------
    // Test 4: Database Connection Isolation (db1 vs db2)
    // ------------------------------------------------------------------------
    printf("6. Testing multi-connection database state isolation (db2 vs db1)...\n");
    sqlite3* db2 = nullptr;
    assert(sqlite3_open(":memory:", &db2) == SQLITE_OK);

    SqliteExtState<VTabSharedState>::get_or_create(db2, [](VTabSharedState* s) {
        s->total_rows_read = 0;
        s->total_rows_inserted = 0;
        s->query_count = 0;
        const char* db2_tag = "NODE_BACKUP";
        memcpy(s->session_tag, db2_tag, strlen(db2_tag) + 1);
        for (int i = 0; i < 5; ++i) s->cache[i] = (i + 1) * 10;
    });

    assert((SqliteVTab::define_with_state<VTabSharedState, StateCacheTable, VTabOptions::Writable>(db2, "state_cache")) == SQLITE_OK);
    assert((SqliteUdf::define_with_state<VTabSharedState, udf_vtab_get_stats>(db2, "vtab_stats", 0)) == SQLITE_OK);
    assert(sqlite3_exec(db2, "CREATE VIRTUAL TABLE db2_cache USING state_cache();", nullptr, nullptr, nullptr) == SQLITE_OK);

    // Verify db2 initial stats
    {
        SqliteStatement stmt(db2, "SELECT vtab_stats();");
        assert(stmt.next());
        assert(strcmp(stmt.column_text(0), "tag=NODE_BACKUP reads=0 inserts=0 queries=0 cache=[10,20,30,40,50]") == 0);
    }

    // Query db2 Virtual Table
    {
        SqliteStatement stmt(db2, "SELECT val, session FROM db2_cache WHERE slot_id = 0;");
        assert(stmt.next());
        assert(stmt.column_int(0) == 10);
        assert(strcmp(stmt.column_text(1), "NODE_BACKUP") == 0);
    }

    // Verify db1 state remained isolated (5 initial reads + 3 scanned rows for slot_id=2 = 8 reads)
    {
        SqliteStatement stmt(db1, "SELECT vtab_stats();");
        assert(stmt.next());
        assert(strcmp(stmt.column_text(0), "tag=NODE_FAILOVER reads=8 inserts=2 queries=2 cache=[100,200,999,400,777]") == 0);
    }

    sqlite3_close(db1);
    sqlite3_close(db2);
    sqlite3_shutdown();

    printf("\nAll 6 Stateful Virtual Table Test Suites Passed Cleanly!\n");
}

int main() {
    run_stateful_vtab_tests();
    return 0;
}
