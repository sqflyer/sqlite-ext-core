#include "sqlite3_ext_creator.h"
#include <string.h>

// ============================================================================
// 1. Connection Shared State (Pure C State Registry)
// ============================================================================
// Pluggable Synchronization Options (Pure C):
// - Default (Read/Write Lock):  SQLITE_EXTENSION_STATE_DECLARE(CAnalyticsState) / _DECLARE_RW
// - 1-Byte Spinlock (TinyLock): SQLITE_EXTENSION_STATE_DECLARE_TINY(CAnalyticsState)
// - SQLite Native Mutex:        SQLITE_EXTENSION_STATE_DECLARE_MUTEX(CAnalyticsState)
// - Generic with custom lock:   SQLITE_EXTENSION_STATE_DECLARE_WITH_LOCK(CAnalyticsState, lock_type)
typedef struct {
    int query_count;
    double total_sum;
    char session_tag[64];
} CAnalyticsState;

// Declare and define the state manager for CAnalyticsState
SQLITE_EXTENSION_STATE_DECLARE(CAnalyticsState)
SQLITE_EXTENSION_STATE_DEFINE(CAnalyticsState)

static void init_state_callback(CAnalyticsState* s) {
    if (!s) return;
    s->query_count = 0;
    s->total_sum = 0.0;
    const char* tag = "C_SESSION";
    memcpy(s->session_tag, tag, strlen(tag) + 1);
}

// ============================================================================
// 2. Stateless Scalar Function: c_math_hypot(a, b) -> sqrt(a^2 + b^2)
// ============================================================================
static void c_math_hypot(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    if (argc < 2) {
        sqlite3_result_error(ctx, "c_math_hypot requires 2 numeric arguments", -1);
        return;
    }
    double a = sqlite3_value_double(argv[0]);
    double b = sqlite3_value_double(argv[1]);
    
    // Freestanding square-root via Newton-Raphson approximation
    double sq = a * a + b * b;
    if (sq <= 0.0) {
        sqlite3_result_double(ctx, 0.0);
        return;
    }
    double root = sq / 2.0;
    for (int i = 0; i < 20; ++i) {
        root = 0.5 * (root + sq / root);
    }
    sqlite3_result_double(ctx, root);
}

// ============================================================================
// 3. Stateful Scalar Function: c_analytics_ping() -> increments query counter
// ============================================================================
static void c_analytics_ping(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    (void)argc;
    (void)argv;
    
    // Fast O(1) state resolution via user_data / auxdata cache
    CAnalyticsState* state = CAnalyticsState_from_context(ctx);
    if (!state) {
        sqlite3_result_error(ctx, "Failed to resolve CAnalyticsState", -1);
        return;
    }
    
    // Increment under thread-safe state lock
    CAnalyticsState_write_acquire(state);
    int current = ++state->query_count;
    CAnalyticsState_write_release(state);
    
    sqlite3_result_int(ctx, current);
}

// ============================================================================
// 4. Custom C Aggregate Function: c_sum_squares(val)
// ============================================================================
typedef struct {
    sqlite3_int64 sum_sq;
} SumSqContext;

static void c_sum_sq_step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    if (argc < 1 || sqlite3_value_type(argv[0]) == SQLITE_NULL) return;
    
    SumSqContext* agg = (SumSqContext*)sqlite3_aggregate_context(ctx, sizeof(SumSqContext));
    if (!agg) {
        sqlite3_result_error_nomem(ctx);
        return;
    }
    sqlite3_int64 val = sqlite3_value_int64(argv[0]);
    agg->sum_sq += (val * val);
}

static void c_sum_sq_final(sqlite3_context* ctx) {
    SumSqContext* agg = (SumSqContext*)sqlite3_aggregate_context(ctx, sizeof(SumSqContext));
    if (!agg) {
        sqlite3_result_int64(ctx, 0);
        return;
    }
    sqlite3_result_int64(ctx, agg->sum_sq);
}

// ============================================================================
// 5. Extension Registration
// ============================================================================
static int register_all_c_components(sqlite3* db) {
    int rc;

    // 1. Initialize per-connection state
    void* pStateEntry = CAnalyticsState_init(db, init_state_callback, NULL);
    if (!pStateEntry) return SQLITE_NOMEM;

    // 2. Register Stateless Scalar UDF
    rc = sqlite3_create_function(
        db, "c_math_hypot", 2, 
        SQLITE_UTF8 | SQLITE_DETERMINISTIC, 
        NULL, c_math_hypot, NULL, NULL
    );
    if (rc != SQLITE_OK) return rc;

    // 3. Register Stateful Scalar UDF using sqlite3_create_function_v2 with state entry and destructor
    rc = sqlite3_create_function_v2(
        db, "c_analytics_ping", 0, 
        SQLITE_UTF8, 
        pStateEntry, c_analytics_ping, NULL, NULL,
        CAnalyticsState_destructor
    );
    if (rc != SQLITE_OK) return rc;

    // 4. Register Custom Aggregate Function
    rc = sqlite3_create_function(
        db, "c_sum_squares", 1, 
        SQLITE_UTF8 | SQLITE_DETERMINISTIC, 
        NULL, NULL, c_sum_sq_step, c_sum_sq_final
    );
    if (rc != SQLITE_OK) return rc;

    return SQLITE_OK;
}

// Named Entrypoint: sqlite3_c_example_init
SQLITE_C_EXTENSION_ENTRYPOINT(c_example, db) {
    return register_all_c_components(db);
}

// Default Entrypoint: sqlite3_extension_init
SQLITE_C_DEFAULT_EXTENSION_ENTRYPOINT(db) {
    return register_all_c_components(db);
}
