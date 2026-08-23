#include "sqlite3_ext_creator.hpp"

// ----------------------------------------------------------------------------
// 1. Stateless Scalar UDFs
// ----------------------------------------------------------------------------
static void stateless_add(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() < 2) {
        ctx.result_error("stateless_add requires 2 arguments");
        return;
    }
    sqlite3_int64 a = args[0].as_int64();
    sqlite3_int64 b = args[1].as_int64();
    ctx.result_int64(a + b);
}

static void stateless_greet(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() < 1) {
        ctx.result_error("stateless_greet requires 1 argument");
        return;
    }
    SqliteStringOwned res(ctx.get());
    res.appendall("Greetings, ");
    SqliteStringView name_view = args[0].as_text();
    res.append(name_view.data(), name_view.length());
    res.appendall("!");
    res.result(ctx);
}

// ----------------------------------------------------------------------------
// 2. Stateless Aggregate: Sum of Squares
// ----------------------------------------------------------------------------
struct StatelessSumSq : public SqliteAggregateBase<sqlite3_int64> {
    sqlite3_int64 total = 0;

    void step(SqliteUdfArgs args) override {
        if (args.size() > 0 && args[0].type() != SQLITE_NULL) {
            sqlite3_int64 v = args[0].as_int64();
            total += (v * v);
        }
    }

    sqlite3_int64 finalize() override {
        return total;
    }
};

// ----------------------------------------------------------------------------
// 3. Stateless TVF: generate_range
// ----------------------------------------------------------------------------
struct StatelessRangeIterator : public SqliteTvfIterator {
    static constexpr const char* schema() {
        return "CREATE TABLE x(value, start hidden, stop hidden)";
    }

    sqlite3_int64 m_current;
    sqlite3_int64 m_stop;

    void init(SqliteUdfArgs args) override {
        m_current = (args.size() > 0 && args[0].type() != SQLITE_NULL) ? args[0].as_int64() : 1;
        m_stop    = (args.size() > 1 && args[1].type() != SQLITE_NULL) ? args[1].as_int64() : 5;
    }

    void next() override {
        m_current++;
    }

    bool eof() const override {
        return m_current > m_stop;
    }

    void column(SqliteContext ctx, int col_idx) override {
        if (col_idx == 0) {
            ctx.result_int64(m_current);
        }
    }

    sqlite3_int64 rowid() const override {
        return m_current;
    }
};

// ----------------------------------------------------------------------------
// 4. Stateless Virtual Table: Fixed Echo Table
// ----------------------------------------------------------------------------
class StatelessEchoCursor : public SqliteVTabCursor {
private:
    int m_row = 0;
public:
    int filter(int, const char*, SqliteUdfArgs) override {
        m_row = 1;
        return SQLITE_OK;
    }
    int next() override {
        m_row++;
        return SQLITE_OK;
    }
    bool eof() override {
        return m_row > 3;
    }
    int column(SqliteContext& ctx, int N) override {
        if (N == 0) ctx.result_int(m_row);
        else if (N == 1) ctx.result_int(m_row * 10);
        return SQLITE_OK;
    }
    int rowid(sqlite3_int64& pRowid) override {
        pRowid = m_row;
        return SQLITE_OK;
    }
};

class StatelessEchoTable : public SqliteVTable {
public:
    StatelessEchoTable(sqlite3* db) : SqliteVTable(db) {}

    static int connect(SqliteConnectArgs& args) {
        int rc = sqlite3_declare_vtab(args.db(), "CREATE TABLE x(id INT, score INT)");
        if (rc == SQLITE_OK) {
            args.set_instance(sqlite_new<StatelessEchoTable>(args.db()));
        }
        return rc;
    }

    int bestIndex(SqliteIndexInfo& info) override {
        info.set_estimated_cost(10.0);
        return SQLITE_OK;
    }

    SqliteVTabCursor* open() override {
        return sqlite_new<StatelessEchoCursor>();
    }
};

// ----------------------------------------------------------------------------
// Entrypoint: stateless_ext
// ----------------------------------------------------------------------------
SQLITE_EXTENSION_ENTRYPOINT(stateless_ext, db) {
    int rc;

    // 1. Scalar UDFs
    rc = SqliteExt::define_scalar(db, "stateless_add", 2, stateless_add);
    if (rc != SQLITE_OK) return rc;

    rc = SqliteExt::define_scalar(db, "stateless_greet", 1, stateless_greet);
    if (rc != SQLITE_OK) return rc;

    // 2. Aggregate
    rc = SqliteExt::define_aggregate<StatelessSumSq>(db, "stateless_sum_sq", 1);
    if (rc != SQLITE_OK) return rc;

    // 3. TVF
    rc = SqliteExt::define_tvf<StatelessRangeIterator>(db, "stateless_range");
    if (rc != SQLITE_OK) return rc;

    // 4. Virtual Table
    rc = SqliteExt::define_vtab<StatelessEchoTable>(db, "stateless_echo");
    if (rc != SQLITE_OK) return rc;

    return SQLITE_OK;
}
