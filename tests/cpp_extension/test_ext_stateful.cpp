#include "sqlite3_ext_creator.hpp"
#include <string.h>

// ----------------------------------------------------------------------------
// Shared State Struct
// ----------------------------------------------------------------------------
struct StatefulSessionState {
    int counter;
    int cache[5];
    char session_tag[64];
};

// ----------------------------------------------------------------------------
// 1. Stateful Scalar UDFs
// ----------------------------------------------------------------------------
static void stateful_inc(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    StatefulSessionState* state = ctx.state<StatefulSessionState>();
    if (!state) {
        ctx.result_error("StatefulSessionState not found");
        return;
    }
    int val = 0;
    {
        SqliteExtState<StatefulSessionState>::WriteGuard lock(state);
        lock->counter++;
        val = lock->counter;
    }
    ctx.result_int(val);
}

static void stateful_get(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    StatefulSessionState* state = ctx.state<StatefulSessionState>();
    if (!state) {
        ctx.result_error("StatefulSessionState not found");
        return;
    }
    int val = 0;
    {
        SqliteExtState<StatefulSessionState>::ReadGuard lock(state);
        val = lock->counter;
    }
    ctx.result_int(val);
}

static void stateful_set_tag(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() < 1 || args[0].type() != SQLITE_TEXT) {
        ctx.result_error("stateful_set_tag requires 1 text argument");
        return;
    }
    StatefulSessionState* state = ctx.state<StatefulSessionState>();
    if (!state) {
        ctx.result_error("StatefulSessionState not found");
        return;
    }
    SqliteStringView tag = args[0].as_text();
    {
        SqliteExtState<StatefulSessionState>::WriteGuard lock(state);
        size_t len = tag.length() < 63 ? tag.length() : 63;
        memcpy(lock->session_tag, tag.data(), len);
        lock->session_tag[len] = '\0';
    }
    ctx.result_int(1);
}

// ----------------------------------------------------------------------------
// 2. Stateful Aggregate: Tagged Concatenation
// ----------------------------------------------------------------------------
struct StatefulTaggedConcat : public SqliteAggregateBase<void> {
    char buffer[256];
    size_t len = 0;

    void step(SqliteContext, SqliteUdfArgs args) {
        if (args.size() > 0 && args[0].type() == SQLITE_TEXT) {
            SqliteStringView str = args[0].as_text();
            if (len > 0 && len + 1 < sizeof(buffer)) {
                buffer[len++] = ',';
            }
            size_t str_len = static_cast<size_t>(str.length());
            size_t to_copy = (sizeof(buffer) - 1 - len < str_len) ? (sizeof(buffer) - 1 - len) : str_len;
            memcpy(buffer + len, str.data(), to_copy);
            len += to_copy;
            buffer[len] = '\0';
        }
    }

    void finalize(SqliteContext ctx) {
        StatefulSessionState* state = ctx.state<StatefulSessionState>();
        SqliteStringOwned res(ctx.get());
        if (state) {
            SqliteExtState<StatefulSessionState>::ReadGuard lock(state);
            res.appendall(lock->session_tag);
            res.appendall(":");
        }
        res.append(buffer, len);
        res.result(ctx);
    }
};

// ----------------------------------------------------------------------------
// 3. Stateful TVF: Stream Cache & State
// ----------------------------------------------------------------------------
struct StatefulMetricsTvf : public SqliteTvfIterator {
    static constexpr const char* schema() {
        return "CREATE TABLE x(metric_name TEXT, metric_value INT)";
    }

    int row_idx = 0;

    void init(SqliteUdfArgs) override {
        row_idx = 0;
    }

    void next() override {
        row_idx++;
    }

    bool eof() const override {
        return row_idx >= 3;
    }

    void column(SqliteContext ctx, int col_idx) override {
        StatefulSessionState* state = ctx.state<StatefulSessionState>();
        if (!state) {
            ctx.result_null();
            return;
        }

        int counter_val = 0, cache0 = 0, cache1 = 0;
        {
            SqliteExtState<StatefulSessionState>::ReadGuard lock(state);
            counter_val = lock->counter;
            cache0 = lock->cache[0];
            cache1 = lock->cache[1];
        }

        if (col_idx == 0) {
            const char* names[] = {"counter", "cache_0", "cache_1"};
            ctx.result_text(names[row_idx]);
        } else if (col_idx == 1) {
            int vals[] = {counter_val, cache0, cache1};
            ctx.result_int(vals[row_idx]);
        }
    }

    sqlite3_int64 rowid() const override {
        return row_idx;
    }
};

// ----------------------------------------------------------------------------
// 4. Stateful Virtual Table: Cache Table
// ----------------------------------------------------------------------------
class StatefulCacheCursor : public SqliteVTabCursor {
private:
    int m_pos = 0;
public:
    int filter(int, const char*, SqliteUdfArgs) override {
        m_pos = 0;
        return SQLITE_OK;
    }
    int next() override {
        m_pos++;
        return SQLITE_OK;
    }
    bool eof() override {
        return m_pos >= 5;
    }
    int column(SqliteContext& ctx, int N) override {
        StatefulSessionState* state = ctx.state<StatefulSessionState>();
        if (!state) {
            ctx.result_null();
            return SQLITE_OK;
        }
        if (N == 0) {
            ctx.result_int(m_pos);
        } else if (N == 1) {
            int val = 0;
            {
                SqliteExtState<StatefulSessionState>::ReadGuard lock(state);
                val = lock->cache[m_pos];
            }
            ctx.result_int(val);
        }
        return SQLITE_OK;
    }
    int rowid(sqlite3_int64& pRowid) override {
        pRowid = m_pos;
        return SQLITE_OK;
    }
};

class StatefulCacheTable : public SqliteVTable {
public:
    StatefulCacheTable(sqlite3* db) : SqliteVTable(db) {}

    static int connect(SqliteConnectArgs& args) {
        int rc = sqlite3_declare_vtab(args.db(), "CREATE TABLE x(slot INT, val INT)");
        if (rc == SQLITE_OK) {
            args.set_instance(sqlite_new<StatefulCacheTable>(args.db()));
        }
        return rc;
    }

    int bestIndex(SqliteIndexInfo& info) override {
        info.set_estimated_cost(5.0);
        return SQLITE_OK;
    }

    SqliteVTabCursor* open() override {
        return sqlite_new<StatefulCacheCursor>();
    }
};

// ----------------------------------------------------------------------------
// Entrypoint: stateful_ext (Using Context-Aware Macro)
// ----------------------------------------------------------------------------
SQLITE_EXTENSION_ENTRYPOINT_CTX(stateful_ext, ctx) {
    SqliteDatabaseView db = ctx.db();

    // 1. Initialize State
    SqliteExt::init_state<StatefulSessionState>(db, [](StatefulSessionState* s) {
        s->counter = 500;
        const char* tag = "SESSION_TEST";
        memcpy(s->session_tag, tag, strlen(tag) + 1);
        for (int i = 0; i < 5; ++i) {
            s->cache[i] = (i + 1) * 111;
        }
    });

    // 2. Register Stateful Scalar UDFs
    int rc = SqliteExt::define_scalar_with_state<StatefulSessionState, stateful_inc>(db, "stateful_inc", 0);
    if (rc != SQLITE_OK) return rc;

    rc = SqliteExt::define_scalar_with_state<StatefulSessionState, stateful_get>(db, "stateful_get", 0);
    if (rc != SQLITE_OK) return rc;

    rc = SqliteExt::define_scalar_with_state<StatefulSessionState, stateful_set_tag>(db, "stateful_set_tag", 1);
    if (rc != SQLITE_OK) return rc;

    // 3. Register Stateful Aggregate
    rc = SqliteExt::define_aggregate_with_state<StatefulSessionState, StatefulTaggedConcat>(db, "stateful_concat", 1);
    if (rc != SQLITE_OK) return rc;

    // 4. Register Stateful TVF
    rc = SqliteExt::define_tvf_with_state<StatefulSessionState, StatefulMetricsTvf>(db, "stateful_metrics");
    if (rc != SQLITE_OK) return rc;

    // 5. Register Stateful Virtual Table
    rc = SqliteExt::define_vtab_with_state<StatefulSessionState, StatefulCacheTable>(db, "stateful_cache");
    if (rc != SQLITE_OK) return rc;

    return SQLITE_OK;
}
