#include "sqlite3_ext_creator.hpp"

// ----------------------------------------------------------------------------
// Shared State for the Stateful Components of Mixed Extension
// ----------------------------------------------------------------------------
struct MixedAuditState {
    int audit_calls;
    double weighted_accumulator;
};

// ----------------------------------------------------------------------------
// 1. Stateless Components
// ----------------------------------------------------------------------------
static void mixed_multiply(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() < 2) {
        ctx.result_error("mixed_multiply requires 2 numeric arguments");
        return;
    }
    double a = args[0].as_double();
    double b = args[1].as_double();
    ctx.result_double(a * b);
}

struct MixedIotaIterator : public SqliteTvfIterator {
    static constexpr const char* schema() {
        return "CREATE TABLE x(val INT, count hidden)";
    }

    int m_cur = 0;
    int m_max = 0;

    void init(SqliteUdfArgs args) override {
        m_cur = 1;
        m_max = (args.size() > 0 && args[0].type() != SQLITE_NULL) ? static_cast<int>(args[0].as_int64()) : 5;
    }

    void next() override {
        m_cur++;
    }

    bool eof() const override {
        return m_cur > m_max;
    }

    void column(SqliteContext ctx, int col_idx) override {
        if (col_idx == 0) ctx.result_int(m_cur);
    }

    sqlite3_int64 rowid() const override {
        return m_cur;
    }
};

// ----------------------------------------------------------------------------
// 2. Stateful Components
// ----------------------------------------------------------------------------
static void mixed_audit_log(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    MixedAuditState* state = ctx.state<MixedAuditState>();
    if (!state) {
        ctx.result_error("MixedAuditState not found");
        return;
    }
    int total = 0;
    {
        SqliteExtState<MixedAuditState>::WriteGuard lock(state);
        lock->audit_calls++;
        total = lock->audit_calls;
    }
    ctx.result_int(total);
}

struct MixedWeightedAvg : public SqliteAggregateBase<double> {
    double sum_weighted = 0.0;
    double sum_weight = 0.0;

    void step(SqliteContext ctx, SqliteUdfArgs args) {
        if (args.size() >= 2 && args[0].type() != SQLITE_NULL && args[1].type() != SQLITE_NULL) {
            double val = args[0].as_double();
            double weight = args[1].as_double();
            sum_weighted += (val * weight);
            sum_weight += weight;

            MixedAuditState* state = ctx.state<MixedAuditState>();
            if (state) {
                SqliteExtState<MixedAuditState>::WriteGuard lock(state);
                lock->weighted_accumulator += (val * weight);
            }
        }
    }

    double finalize() {
        return sum_weight > 0.0 ? (sum_weighted / sum_weight) : 0.0;
    }
};

// ----------------------------------------------------------------------------
// Default Entrypoint: sqlite3_extension_init
// ----------------------------------------------------------------------------
SQLITE_DEFAULT_EXTENSION_ENTRYPOINT(db) {
    // 1. Initialize State for stateful components
    SqliteExt::init_state<MixedAuditState>(db, [](MixedAuditState* s) {
        s->audit_calls = 0;
        s->weighted_accumulator = 0.0;
    });

    // 2. Register Stateless Components
    int rc = SqliteExt::define_scalar(db, "mixed_multiply", 2, mixed_multiply);
    if (rc != SQLITE_OK) return rc;

    rc = SqliteExt::define_tvf<MixedIotaIterator>(db, "mixed_iota");
    if (rc != SQLITE_OK) return rc;

    // 3. Register Stateful Components
    rc = SqliteExt::define_scalar_with_state<MixedAuditState, mixed_audit_log>(db, "mixed_audit", 0);
    if (rc != SQLITE_OK) return rc;

    rc = SqliteExt::define_aggregate_with_state<MixedAuditState, MixedWeightedAvg>(db, "mixed_weighted_avg", 2);
    if (rc != SQLITE_OK) return rc;

    return SQLITE_OK;
}
