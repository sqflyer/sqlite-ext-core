#include "sqlite3_ext_creator.hpp"
#include "async/sqlite3_coro_ext_pool.hpp"
#include "sqlite3_atomic.hpp"
#include <stdio.h>

// ============================================================================
// 1. Tagged Extension-Presence Pool Configuration
// ============================================================================
// Unique tag type guaranteeing this extension's worker pool is isolated from
// any other extensions loaded in the same SQLite process.

struct CoroCppExtTag {};
using ExampleCoroPool = SqliteExtCoroPool<CoroCppExtTag>;

// Global atomic metrics for this extension
static SqliteAtomicInt g_cpp_total_tasks(0);
static SqliteAtomicInt g_cpp_global_sum(0);

// ============================================================================
// 2. C++ Scalar User-Defined Functions (UDFs)
// ============================================================================

// SQL: SELECT coro_cpp_spawn(db_id, item_id, multiplier);
// Spawns a stateful capturing lambda closure into this extension's worker pool
static void sql_coro_cpp_spawn(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() < 3) {
        ctx.result_error("coro_cpp_spawn requires (db_id, item_id, multiplier)");
        return;
    }

    int db_id = (int)args[0].as_int64();
    int item_id = (int)args[1].as_int64();
    int multiplier = (int)args[2].as_int64();

    // Universal type-safe template spawn into this extension's tagged pool
    bool ok = sqlite_coro_ext_spawn<CoroCppExtTag>([db_id, item_id, multiplier]() {
        (void)db_id;
        // Stage 1: Initial computation for the calling database
        int intermediate = item_id * multiplier;

        // Cooperatively yield CPU control so other database fibers can run
        SqliteCoroScheduler::yield();

        // Stage 2: Second processing phase
        intermediate += 200;

        SqliteCoroScheduler::yield();

        // Stage 3: Atomic accumulation into extension metrics
        g_cpp_global_sum += intermediate;
        g_cpp_total_tasks += 1;
    });

    if (!ok) {
        ctx.result_error("Failed to enqueue closure in extension pool");
        return;
    }

    ctx.result_text("ENQUEUED_IN_CPP_EXTENSION_POOL");
}

// SQL: SELECT coro_cpp_wait();
// Synchronously waits until all tasks across all databases in this pool complete
static void sql_coro_cpp_wait(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    ExampleCoroPool::wait_all();
    ctx.result_text("CPP_EXTENSION_POOL_DRAINED");
}

// SQL: SELECT coro_cpp_global_sum();
// Returns total accumulated sum across all database connections
static void sql_coro_cpp_global_sum(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    ctx.result_int(g_cpp_global_sum.load());
}

// SQL: SELECT coro_cpp_tasks_completed();
// Returns total tasks processed in this extension's shared pool
static void sql_coro_cpp_tasks_completed(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    ctx.result_int(g_cpp_total_tasks.load());
}

// SQL: SELECT coro_cpp_ref_count();
// Returns count of active database connections sharing this extension pool
static void sql_coro_cpp_ref_count(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    ctx.result_int(ExampleCoroPool::ref_count());
}

// ============================================================================
// 3. Extension Lifecycle & Registration
// ============================================================================

static void on_db_disconnect(void* arg) {
    (void)arg;
    // Decrement database connection reference count for this extension tag
    ExampleCoroPool::release();
}

static int register_coro_cpp_extension(SqliteDatabaseView db) {
    // 1. Acquire reference to the tagged extension presence worker pool (4 workers)
    SqliteCoroScheduler* pool = ExampleCoroPool::acquire(4);
    if (!pool) return SQLITE_NOMEM;

    // 2. Register scalar functions with disconnection callback attached
    sqlite3_create_function_v2(
        db.get(), "coro_cpp_spawn", 3, SQLITE_UTF8, nullptr,
        [](sqlite3_context* c, int argc, sqlite3_value** argv) {
            SqliteContext ctx(c);
            SqliteUdfArgs args(argv, argc);
            sql_coro_cpp_spawn(ctx, args);
        },
        nullptr, nullptr, on_db_disconnect
    );

    SqliteExt::define_scalar(db, "coro_cpp_wait", 0, sql_coro_cpp_wait);
    SqliteExt::define_scalar(db, "coro_cpp_global_sum", 0, sql_coro_cpp_global_sum);
    SqliteExt::define_scalar(db, "coro_cpp_tasks_completed", 0, sql_coro_cpp_tasks_completed);
    SqliteExt::define_scalar(db, "coro_cpp_ref_count", 0, sql_coro_cpp_ref_count);

    return SQLITE_OK;
}

// Named Entrypoint: sqlite3_libcoro_cpp_example_init
SQLITE_EXTENSION_ENTRYPOINT(libcoro_cpp_example, db) {
    return register_coro_cpp_extension(db);
}

// Named Entrypoint: sqlite3_coro_cpp_example_init
SQLITE_EXTENSION_ENTRYPOINT(coro_cpp_example, db) {
    return register_coro_cpp_extension(db);
}

// Default Entrypoint: sqlite3_extension_init
SQLITE_DEFAULT_EXTENSION_ENTRYPOINT(db) {
    return register_coro_cpp_extension(db);
}
