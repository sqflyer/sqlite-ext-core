#include "sqlite3_ext_creator.hpp"
#include <string.h>

// ============================================================================
// 1. Connection-Bound Shared State
// ============================================================================
// Shared state is managed via SqliteExtState<T, LockPolicy>.
// Lock Policy Options:
// - Default (Read/Write Lock):  SqliteExtState<AnalyticsState> (or SqliteExtStateRw<AnalyticsState>)
// - 1-Byte Spinlock (TinyLock): SqliteExtStateTiny<AnalyticsState> (or SqliteExtState<AnalyticsState, SqliteTinyLock>)
// - SQLite Native Mutex:        SqliteExtStateMutex<AnalyticsState> (or SqliteExtState<AnalyticsState, SqliteMutex>)
struct AnalyticsState {
    int total_queries;
    double running_sum;
    char session_tag[64];
};

// ============================================================================
// 2. Scalar User-Defined Functions (UDFs)
// ============================================================================

// Stateless Scalar: math_hypot(a, b) -> sqrt(a^2 + b^2)
static void math_hypot(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() < 2) {
        ctx.result_error("math_hypot requires 2 numeric arguments");
        return;
    }
    double a = args[0].as_double();
    double b = args[1].as_double();
    
    // Freestanding square-root via Newton-Raphson approximation
    double sq = a * a + b * b;
    if (sq <= 0.0) {
        ctx.result_double(0.0);
        return;
    }
    double root = sq / 2.0;
    for (int i = 0; i < 20; ++i) {
        root = 0.5 * (root + sq / root);
    }
    ctx.result_double(root);
}

// Stateful Scalar: analytics_ping() -> increments per-connection query counter
static void analytics_ping(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    AnalyticsState* state = ctx.state<AnalyticsState>();
    if (!state) {
        ctx.result_error("AnalyticsState not initialized");
        return;
    }
    int count = 0;
    {
        SqliteExtState<AnalyticsState>::WriteGuard lock(state);
        lock->total_queries++;
        count = lock->total_queries;
    }
    ctx.result_int(count);
}

// Fallible Scalar: text_repeat(str, count) -> repeats string using SqliteResult<SqliteString>
static void text_repeat(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() < 2) {
        ctx.result_error("text_repeat requires (text, count)");
        return;
    }
    SqliteStringView text = args[0].as_text();
    int count = static_cast<int>(args[1].as_int64());
    if (count < 0) count = 0;

    // Fallible buffer allocation returning SqliteResult<SqliteString>
    auto res_buf = SqliteString::try_create();
    if (res_buf.is_err()) {
        res_buf.set_sqlite_err(ctx.get());
        return;
    }
    SqliteString str = res_buf.take_value();
    SqliteStatus reserve_stat = str.try_reserve(text.length() * count + 1);
    if (reserve_stat.is_err()) {
        reserve_stat.set_sqlite_err(ctx.get());
        return;
    }
    for (int i = 0; i < count; ++i) {
        SqliteStatus stat = str.try_append(text.data(), text.length());
        if (stat.is_err()) {
            stat.set_sqlite_err(ctx.get());
            return;
        }
    }
    ctx.result_text(str.c_str(), str.length());
}


// ============================================================================
// 3. Object-Oriented Aggregate Function
// ============================================================================

// Geometric Mean: nth-root of product of positive values
struct GeometricMeanAgg : public SqliteAggregateBase<double> {
    double product = 1.0;
    int count = 0;

    void step(SqliteUdfArgs args) override {
        if (args.size() > 0 && args[0].type() != SQLITE_NULL) {
            double val = args[0].as_double();
            if (val > 0.0) {
                product *= val;
                count++;
            }
        }
    }

    double finalize() override {
        if (count == 0) return 0.0;
        // nth-root approximation
        double root = product;
        for (int i = 0; i < 30; ++i) {
            double p = 1.0;
            for (int k = 0; k < count - 1; ++k) p *= root;
            if (p != 0.0) root = ((count - 1) * root + product / p) / count;
        }
        return root;
    }
};

// ============================================================================
// 4. Table-Valued Function (TVF): fibonacci(n)
// ============================================================================
struct FibonacciIterator : public SqliteTvfIterator {
    static constexpr const char* schema() {
        return "CREATE TABLE x(idx INT, val INT, max_n hidden)";
    }

    int m_max = 10;
    int m_idx = 0;
    sqlite3_int64 m_curr = 0;
    sqlite3_int64 m_next = 1;

    void init(SqliteUdfArgs args) override {
        m_idx = 1;
        m_curr = 1;
        m_next = 1;
        m_max = (args.size() > 0 && args[0].type() != SQLITE_NULL) ? static_cast<int>(args[0].as_int64()) : 10;
    }

    void next() override {
        m_idx++;
        sqlite3_int64 tmp = m_curr + m_next;
        m_curr = m_next;
        m_next = tmp;
    }

    bool eof() const override {
        return m_idx > m_max;
    }

    void column(SqliteContext ctx, int col_idx) override {
        if (col_idx == 0) {
            ctx.result_int(m_idx);
        } else if (col_idx == 1) {
            ctx.result_int64(m_curr);
        }
    }

    sqlite3_int64 rowid() const override {
        return m_idx;
    }
};

// ============================================================================
// 5. Extension Entrypoint: Named (sqlite3_example_init) & Default (sqlite3_extension_init)
// ============================================================================

static int register_all_components(SqliteDatabaseView db) {
    // 1. Initialize shared connection state
    SqliteExt::init_state<AnalyticsState>(db, [](AnalyticsState* s) {
        s->total_queries = 0;
        s->running_sum = 0.0;
        const char* tag = "EXAMPLE_SESSION";
        memcpy(s->session_tag, tag, strlen(tag) + 1);
    });

    // 2. Register Scalar UDFs
    int rc = SqliteExt::define_scalar(db, "math_hypot", 2, math_hypot);
    if (rc != SQLITE_OK) return rc;

    rc = SqliteExt::define_scalar(db, "text_repeat", 2, text_repeat);
    if (rc != SQLITE_OK) return rc;

    rc = SqliteExt::define_scalar_with_state<AnalyticsState, analytics_ping>(db, "analytics_ping", 0);
    if (rc != SQLITE_OK) return rc;


    // 3. Register Aggregate
    rc = SqliteExt::define_aggregate<GeometricMeanAgg>(db, "geo_mean", 1);
    if (rc != SQLITE_OK) return rc;

    // 4. Register TVF
    rc = SqliteExt::define_tvf<FibonacciIterator>(db, "fibonacci");
    if (rc != SQLITE_OK) return rc;

    return SQLITE_OK;
}

// Named Entrypoint: sqlite3_example_init
SQLITE_EXTENSION_ENTRYPOINT(example, db) {
    return register_all_components(db);
}

// Default Entrypoint: sqlite3_extension_init
SQLITE_DEFAULT_EXTENSION_ENTRYPOINT(db) {
    return register_all_components(db);
}
