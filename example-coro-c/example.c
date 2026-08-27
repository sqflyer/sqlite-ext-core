#include "sqlite3_ext_creator.h"
#include "async/sqlite3_coro_ext_pool.h"
#include "sqlite3_atomic.h"
#include "sqlite3_time.h"
#include <stdio.h>
#include <string.h>

// Declare collision-proof static tag token for this Pure C extension
SQLITE_EXT_TAG_DECLARE(CoroCExtTag);

// ============================================================================
// 1. Extension-Presence Shared State (Process-Wide Metrics)
// ============================================================================

static sqlite3_atomic_int g_total_tasks = 0;
static sqlite3_atomic_int g_global_sum  = 0;

// ============================================================================
// 2. Task Payload & Cooperative Fiber Routine
// ============================================================================

typedef struct {
    int db_id;
    int item_id;
    int multiplier;
} SharedTaskPayload;

static void extension_worker_fiber(void* arg) {
    SharedTaskPayload* p = (SharedTaskPayload*)arg;

    // Stage 1: Initial computation for the requesting database
    int intermediate = p->item_id * p->multiplier;

    // Cooperatively yield CPU control to let fibers from other databases run
    sqlite3_coro_pool_yield();

    // Stage 2: Second processing phase
    intermediate += 100;

    sqlite3_coro_pool_yield();

    // Stage 3: Atomic accumulation into extension metrics
    sqlite3_atomic_fetch_add(&g_global_sum, intermediate);
    sqlite3_atomic_fetch_add(&g_total_tasks, 1);

    // Free the dynamically allocated task payload
    sqlite3_free(p);
}

// ============================================================================
// 3. SQL User-Defined Functions (UDFs)
// ============================================================================

// SQL: SELECT coro_c_spawn(db_id, item_id, multiplier);
// Dispatches a task to this extension's collision-proof tagged worker pool
static void sql_coro_c_spawn(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    if (argc < 3) {
        sqlite3_result_error(ctx, "coro_c_spawn requires (db_id, item_id, multiplier)", -1);
        return;
    }

    int db_id = sqlite3_value_int(argv[0]);
    int item_id = sqlite3_value_int(argv[1]);
    int multiplier = sqlite3_value_int(argv[2]);

    SharedTaskPayload* p = (SharedTaskPayload*)sqlite3_malloc64(sizeof(SharedTaskPayload));
    if (!p) {
        sqlite3_result_error_nomem(ctx);
        return;
    }
    p->db_id = db_id;
    p->item_id = item_id;
    p->multiplier = multiplier;

    // Retrieve pointer to this extension's tagged pool without incrementing ref count
    sqlite3_coro_pool_t* pool = sqlite3_coro_ext_pool_get(SQLITE_EXT_TAG(CoroCExtTag));
    if (!pool) {
        pool = sqlite3_coro_ext_pool_acquire(SQLITE_EXT_TAG(CoroCExtTag), 4);
    }
    if (!pool) {
        sqlite3_free(p);
        sqlite3_result_error_nomem(ctx);
        return;
    }

    // Enqueue task into the extension's dedicated worker pool
    int rc = sqlite3_coro_pool_spawn(pool, extension_worker_fiber, p, 0);
    if (rc != SQLITE_OK) {
        sqlite3_free(p);
        sqlite3_result_error(ctx, "Failed to enqueue task in extension pool", -1);
        return;
    }

    sqlite3_result_text(ctx, "ENQUEUED_IN_EXTENSION_POOL", -1, SQLITE_STATIC);
}

// SQL: SELECT coro_c_wait();
// Synchronously waits until all tasks in this tagged extension pool complete
static void sql_coro_c_wait(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    (void)argc; (void)argv;
    sqlite3_coro_ext_pool_wait(SQLITE_EXT_TAG(CoroCExtTag));
    sqlite3_result_text(ctx, "EXTENSION_POOL_DRAINED", -1, SQLITE_STATIC);
}

// SQL: SELECT coro_c_global_sum();
// Returns accumulated sum across all database connections
static void sql_coro_c_global_sum(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    (void)argc; (void)argv;
    sqlite3_result_int(ctx, sqlite3_atomic_load(&g_global_sum));
}

// SQL: SELECT coro_c_tasks_completed();
// Returns total tasks completed in this tagged extension pool
static void sql_coro_c_tasks_completed(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    (void)argc; (void)argv;
    sqlite3_result_int(ctx, sqlite3_atomic_load(&g_total_tasks));
}

// SQL: SELECT coro_c_ref_count();
// Returns count of active database connections sharing this extension pool
static void sql_coro_c_ref_count(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    (void)argc; (void)argv;
    sqlite3_result_int(ctx, sqlite3_coro_ext_pool_ref_count(SQLITE_EXT_TAG(CoroCExtTag)));
}

// ============================================================================
// 4. Extension Lifecycle (Atomic Reference Counting)
// ============================================================================

static void on_db_disconnect(void* arg) {
    (void)arg;
    // Release connection reference to this tagged extension pool
    sqlite3_coro_ext_pool_release(SQLITE_EXT_TAG(CoroCExtTag));
}

static int register_coro_c_extension(sqlite3* db) {
    // Acquire the shared extension presence worker pool (4 background OS threads)
    sqlite3_coro_pool_t* pool = sqlite3_coro_ext_pool_acquire(SQLITE_EXT_TAG(CoroCExtTag), 4);
    if (!pool) return SQLITE_NOMEM;

    // Register scalar functions with disconnection callback attached
    sqlite3_create_function_v2(db, "coro_c_spawn", 3, SQLITE_UTF8, NULL,
                               sql_coro_c_spawn, NULL, NULL, on_db_disconnect);
    sqlite3_create_function(db, "coro_c_wait", 0, SQLITE_UTF8, NULL,
                            sql_coro_c_wait, NULL, NULL);
    sqlite3_create_function(db, "coro_c_global_sum", 0, SQLITE_UTF8, NULL,
                            sql_coro_c_global_sum, NULL, NULL);
    sqlite3_create_function(db, "coro_c_tasks_completed", 0, SQLITE_UTF8, NULL,
                            sql_coro_c_tasks_completed, NULL, NULL);
    sqlite3_create_function(db, "coro_c_ref_count", 0, SQLITE_UTF8, NULL,
                            sql_coro_c_ref_count, NULL, NULL);

    return SQLITE_OK;
}

// Named Entrypoint: sqlite3_libcoro_c_example_init
SQLITE_C_EXTENSION_ENTRYPOINT(libcoro_c_example, db) {
    return register_coro_c_extension(db);
}

// Named Entrypoint: sqlite3_coro_c_example_init
SQLITE_C_EXTENSION_ENTRYPOINT(coro_c_example, db) {
    return register_coro_c_extension(db);
}

// Default Entrypoint: sqlite3_extension_init
SQLITE_C_DEFAULT_EXTENSION_ENTRYPOINT(db) {
    return register_coro_c_extension(db);
}
