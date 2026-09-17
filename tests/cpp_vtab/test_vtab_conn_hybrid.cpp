#define SQLITE_CORE
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sqlite3.h>

#include "../../include/sqlite3_ext.hpp"

// ============================================================================
// 1. Domain State Structures
// ============================================================================

struct SharedAppMetrics {
    int global_query_count;
    int global_total_items;
};

struct ConnectionSession {
    int session_id;
    int local_counter;
    char session_name[32];
};

// ============================================================================
// 2. Connection-State Virtual Table
// ============================================================================

class ConnCursor : public SqliteVTabCursor {
private:
    int m_pos;
    int m_max;
    ConnectionSession* m_session;

public:
    ConnCursor(ConnectionSession* s) : m_pos(0), m_max(3), m_session(s) {}

    int filter(int idxNum, const char* idxStr, SqliteUdfArgs args) override {
        (void)idxNum; (void)idxStr; (void)args;
        m_pos = 0;
        if (m_session) {
            m_session->local_counter++;
        }
        return SQLITE_OK;
    }

    int next() override {
        m_pos++;
        return SQLITE_OK;
    }

    bool eof() override {
        return m_pos >= m_max;
    }

    int column(SqliteContext& ctx, int N) override {
        ConnectionSession* s = ctx.conn_state<ConnectionSession>();
        assert(s != nullptr);
        if (N == 0) {
            ctx.result_int(m_pos);
        } else if (N == 1) {
            ctx.result_int(s->session_id);
        } else if (N == 2) {
            ctx.result_text(s->session_name);
        } else if (N == 3) {
            ctx.result_int(s->local_counter);
        }
        return SQLITE_OK;
    }

    int rowid(sqlite3_int64& pRowid) override {
        pRowid = m_pos;
        return SQLITE_OK;
    }
};

class ConnTable : public SqliteVTable {
private:
    ConnectionSession* m_session;

public:
    ConnTable(sqlite3* db, ConnectionSession* s)
        : SqliteVTable(db), m_session(s) {}

    static int connect(SqliteConnectArgs& args) {
        int rc = sqlite3_declare_vtab(args.db(), "CREATE TABLE x(row_id INT, session_id INT, name TEXT, counter INT)");
        if (rc == SQLITE_OK) {
            ConnectionSession* s = args.conn_state<ConnectionSession>();
            assert(s != nullptr);
            args.set_instance(sqlite_new<ConnTable>(args.db(), s));
        }
        return rc;
    }

    int bestIndex(SqliteIndexInfo& info) override {
        info.set_estimated_cost(1.0);
        return SQLITE_OK;
    }

    SqliteVTabCursor* open() override {
        return sqlite_new<ConnCursor>(m_session);
    }
};

// ============================================================================
// 3. Hybrid-State Virtual Table
// ============================================================================

class HybridCursor : public SqliteVTabCursor {
private:
    int m_pos;
    int m_max;
    SharedAppMetrics* m_shared;
    ConnectionSession* m_session;

public:
    HybridCursor(SharedAppMetrics* shared, ConnectionSession* session)
        : m_pos(0), m_max(2), m_shared(shared), m_session(session) {}

    int filter(int idxNum, const char* idxStr, SqliteUdfArgs args) override {
        (void)idxNum; (void)idxStr; (void)args;
        m_pos = 0;
        if (m_shared) {
            SqliteRwLock lock;
            lock.lock_write();
            m_shared->global_query_count++;
            lock.unlock_write();
        }
        if (m_session) {
            m_session->local_counter++;
        }
        return SQLITE_OK;
    }

    int next() override {
        m_pos++;
        return SQLITE_OK;
    }

    bool eof() override {
        return m_pos >= m_max;
    }

    int column(SqliteContext& ctx, int N) override {
        auto ext = ctx.hybrid_ext<SharedAppMetrics, ConnectionSession>();
        auto conn = ctx.hybrid_conn<SharedAppMetrics, ConnectionSession>();
        assert(ext != nullptr);
        assert(conn != nullptr);

        if (N == 0) {
            ctx.result_int(m_pos);
        } else if (N == 1) {
            ctx.result_int(ext->global_query_count);
        } else if (N == 2) {
            ctx.result_int(conn->session_id);
        } else if (N == 3) {
            ctx.result_text(conn->session_name);
        }
        return SQLITE_OK;
    }

    int rowid(sqlite3_int64& pRowid) override {
        pRowid = m_pos;
        return SQLITE_OK;
    }
};

class HybridTable : public SqliteVTable {
private:
    SharedAppMetrics* m_shared;
    ConnectionSession* m_session;

public:
    HybridTable(sqlite3* db, SharedAppMetrics* shared, ConnectionSession* session)
        : SqliteVTable(db), m_shared(shared), m_session(session) {}

    static int connect(SqliteConnectArgs& args) {
        int rc = sqlite3_declare_vtab(args.db(), "CREATE TABLE x(row_id INT, global_queries INT, session_id INT, name TEXT)");
        if (rc == SQLITE_OK) {
            auto ext = args.hybrid_ext<SharedAppMetrics, ConnectionSession>();
            auto conn = args.hybrid_conn<SharedAppMetrics, ConnectionSession>();
            assert(ext != nullptr);
            assert(conn != nullptr);
            args.set_instance(sqlite_new<HybridTable>(args.db(), ext, conn));
        }
        return rc;
    }

    int bestIndex(SqliteIndexInfo& info) override {
        info.set_estimated_cost(1.0);
        return SQLITE_OK;
    }

    SqliteVTabCursor* open() override {
        return sqlite_new<HybridCursor>(m_shared, m_session);
    }
};

// ============================================================================
// 4. Companion UDFs (Conn State & Hybrid State)
// ============================================================================

static void udf_conn_bump(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    ConnectionSession* s = ctx.conn_state<ConnectionSession>();
    assert(s != nullptr);
    s->local_counter += 10;
    ctx.result_int(s->local_counter);
}

static void udf_hybrid_bump(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    auto ext = ctx.hybrid_ext<SharedAppMetrics, ConnectionSession>();
    auto conn = ctx.hybrid_conn<SharedAppMetrics, ConnectionSession>();
    assert(ext != nullptr);
    assert(conn != nullptr);

    ext->global_total_items += 100;
    conn->local_counter += 1;

    char buf[128];
    snprintf(buf, sizeof(buf), "global_items=%d,session=%s,counter=%d",
             ext->global_total_items,
             conn->session_name,
             conn->local_counter);
    ctx.result_text(buf);
}

// ============================================================================
// 5. Hybrid State TVF Iterator
// ============================================================================

struct HybridTvfIterator : public SqliteTvfIterator {
    int m_idx;
    int m_max;

    static constexpr const char* schema() {
        return "CREATE TABLE x(idx INT, global_items INT, session_name TEXT)";
    }

    HybridTvfIterator() : m_idx(0), m_max(2) {}

    void init(SqliteUdfArgs args) override {
        (void)args;
        m_idx = 0;
    }

    void next() override {
        m_idx++;
    }

    bool eof() const override {
        return m_idx >= m_max;
    }

    void column(SqliteContext ctx, int N) override {
        auto ext = ctx.hybrid_ext<SharedAppMetrics, ConnectionSession>();
        auto conn = ctx.hybrid_conn<SharedAppMetrics, ConnectionSession>();
        assert(ext != nullptr);
        assert(conn != nullptr);

        if (N == 0) {
            ctx.result_int(m_idx);
        } else if (N == 1) {
            ctx.result_int(ext->global_total_items);
        } else if (N == 2) {
            ctx.result_text(conn->session_name);
        }
    }

    sqlite3_int64 rowid() const override {
        return m_idx;
    }
};

// ============================================================================
// 6. Hybrid State Aggregate
// ============================================================================

class HybridSumAggregate : public SqliteAggregateBase<int64_t> {
private:
    int64_t m_sum = 0;

public:
    void step(SqliteContext ctx, SqliteUdfArgs args) override {
        if (args.empty()) return;
        m_sum += args[0].as_int64();

        auto conn = ctx.hybrid_conn<SharedAppMetrics, ConnectionSession>();
        if (conn) {
            conn->local_counter++;
        }
    }

    int64_t finalize() override {
        return m_sum;
    }
};

// ============================================================================
// 7. Main Test Suite
// ============================================================================

int main() {
    sqlite3_initialize();
    printf("=== Starting Conn State & Hybrid State Integration Tests ===\n");

    // ------------------------------------------------------------------------
    // Part A: Connection-State VTab & UDF Isolation Test
    // ------------------------------------------------------------------------
    printf("\n--- Test A: Connection State Isolation Across Multiple DB Handles ---\n");
    sqlite3* db1 = nullptr;
    sqlite3* db2 = nullptr;
    assert(sqlite3_open(":memory:", &db1) == SQLITE_OK);
    assert(sqlite3_open(":memory:", &db2) == SQLITE_OK);

    // Register ConnTable and udf_conn_bump on both connections via SqliteExt facade (defaults init)
    assert((SqliteExt::define_vtab_with_conn_state<ConnectionSession, ConnTable>(db1, "conn_tab")) == SQLITE_OK);
    assert((SqliteExt::define_scalar_with_conn_state<ConnectionSession, udf_conn_bump>(db1, "conn_bump", 0)) == SQLITE_OK);
    assert(sqlite3_exec(db1, "CREATE VIRTUAL TABLE vtab_alice USING conn_tab();", nullptr, nullptr, nullptr) == SQLITE_OK);

    assert((SqliteExt::define_vtab_with_conn_state<ConnectionSession, ConnTable>(db2, "conn_tab")) == SQLITE_OK);
    assert((SqliteExt::define_scalar_with_conn_state<ConnectionSession, udf_conn_bump>(db2, "conn_bump", 0)) == SQLITE_OK);
    assert(sqlite3_exec(db2, "CREATE VIRTUAL TABLE vtab_bob USING conn_tab();", nullptr, nullptr, nullptr) == SQLITE_OK);

    // Customize conn state on db1
    SqliteExt::init_conn_state<ConnectionSession>(db1, [](ConnectionSession* s) {
        s->session_id = 101;
        s->local_counter = 0;
        snprintf(s->session_name, sizeof(s->session_name), "CONN_ALICE");
    });

    // Customize conn state on db2
    SqliteExt::init_conn_state<ConnectionSession>(db2, [](ConnectionSession* s) {
        s->session_id = 202;
        s->local_counter = 50;
        snprintf(s->session_name, sizeof(s->session_name), "CONN_BOB");
    });

    // Query vtab on db1
    {
        SqliteStatement stmt(db1, "SELECT row_id, session_id, name, counter FROM vtab_alice;");
        int rows = 0;
        while (stmt.next()) {
            assert(stmt.column_int(0) == rows);
            assert(stmt.column_int(1) == 101);
            assert(strcmp(stmt.column_text(2), "CONN_ALICE") == 0);
            assert(stmt.column_int(3) == 1); // filter incremented once
            rows++;
        }
        assert(rows == 3);
    }

    // Call UDF on db1
    {
        SqliteStatement stmt(db1, "SELECT conn_bump();");
        assert(stmt.next());
        assert(stmt.column_int(0) == 11); // 1 + 10 = 11
    }

    // Verify db2 is completely isolated and retains BOB's state
    {
        SqliteStatement stmt(db2, "SELECT row_id, session_id, name, counter FROM vtab_bob;");
        int rows = 0;
        while (stmt.next()) {
            assert(stmt.column_int(0) == rows);
            assert(stmt.column_int(1) == 202);
            assert(strcmp(stmt.column_text(2), "CONN_BOB") == 0);
            assert(stmt.column_int(3) == 51); // filter incremented 50 -> 51
            rows++;
        }
        assert(rows == 3);
    }

    // Call UDF on db2
    {
        SqliteStatement stmt(db2, "SELECT conn_bump();");
        assert(stmt.next());
        assert(stmt.column_int(0) == 61); // 51 + 10 = 61
    }

    printf("Test A Passed: Connection-unique states operate independently.\n");

    // ------------------------------------------------------------------------
    // Part B: Hybrid State Virtual Table & UDF Registration
    // ------------------------------------------------------------------------
    printf("\n--- Test B: Hybrid State Virtual Table, UDF, TVF, & Aggregate ---\n");

    // Register Hybrid Table via SqliteExt (defaults init during extension loading)
    assert((SqliteExt::define_vtab_with_hybrid_state<SharedAppMetrics, ConnectionSession, HybridTable>(db1, "hybrid_tab")) == SQLITE_OK);
    assert(sqlite3_exec(db1, "CREATE VIRTUAL TABLE vtab_hybrid USING hybrid_tab();", nullptr, nullptr, nullptr) == SQLITE_OK);

    // Register Hybrid Scalar UDF
    assert((SqliteExt::define_scalar_with_hybrid_state<SharedAppMetrics, ConnectionSession, udf_hybrid_bump>(db1, "hybrid_bump", 0)) == SQLITE_OK);

    // Register Hybrid TVF
    assert((SqliteExt::define_tvf_with_hybrid_state<SharedAppMetrics, ConnectionSession, HybridTvfIterator>(db1, "hybrid_tvf")) == SQLITE_OK);

    // Register Hybrid Aggregate
    assert((SqliteExt::define_aggregate_with_hybrid_state<SharedAppMetrics, ConnectionSession, HybridSumAggregate>(db1, "hybrid_sum", 1)) == SQLITE_OK);

    // Initialize/customize shared extension state
    SqliteExt::init_state<SharedAppMetrics>(db1, [](SharedAppMetrics* m) {
        m->global_query_count = 0;
        m->global_total_items = 500;
    });

    // Test querying hybrid virtual table
    {
        SqliteStatement stmt(db1, "SELECT row_id, global_queries, session_id, name FROM vtab_hybrid;");
        int rows = 0;
        while (stmt.next()) {
            assert(stmt.column_int(0) == rows);
            assert(stmt.column_int(1) == 1); // filter incremented global_query_count to 1
            assert(stmt.column_int(2) == 101);
            assert(strcmp(stmt.column_text(3), "CONN_ALICE") == 0);
            rows++;
        }
        assert(rows == 2);
    }

    // Test hybrid UDF
    {
        SqliteStatement stmt(db1, "SELECT hybrid_bump();");
        assert(stmt.next());
        const char* res = stmt.column_text(0);
        // items went 500 -> 600, counter went 11 + 1 (filter) + 1 = 13
        assert(strcmp(res, "global_items=600,session=CONN_ALICE,counter=13") == 0);
    }

    // Test hybrid TVF
    {
        SqliteStatement stmt(db1, "SELECT idx, global_items, session_name FROM hybrid_tvf();");
        int rows = 0;
        while (stmt.next()) {
            assert(stmt.column_int(0) == rows);
            assert(stmt.column_int(1) == 600); // from shared ext state
            assert(strcmp(stmt.column_text(2), "CONN_ALICE") == 0); // from conn state
            rows++;
        }
        assert(rows == 2);
    }

    // Test hybrid Aggregate
    {
        SqliteStatement stmt(db1, "SELECT hybrid_sum(val) FROM (SELECT 10 AS val UNION ALL SELECT 20 UNION ALL SELECT 30);");
        assert(stmt.next());
        assert(stmt.column_int64(0) == 60);
    }

    // Verify counter incremented by aggregate steps (3 rows) -> 13 + 3 = 16
    {
        ConnectionSession* s = SqliteExt::get_conn_state<ConnectionSession>(db1);
        assert(s != nullptr);
        assert(s->local_counter == 16);
    }

    printf("Test B Passed: Hybrid state seamlessly dispatched across VTab, UDF, TVF, and Aggregate.\n");

    // ------------------------------------------------------------------------
    // Part C: Cleanup and Verification of Clean Teardown
    // ------------------------------------------------------------------------
    printf("\n--- Test C: Clean Teardown ---\n");
    assert(sqlite3_close(db1) == SQLITE_OK);
    assert(sqlite3_close(db2) == SQLITE_OK);
    sqlite3_shutdown();

    printf("Test C Passed: All database connections closed with zero leaks.\n");
    printf("\n=== All Conn State & Hybrid State Integration Tests Succeeded! ===\n");
    return 0;
}
