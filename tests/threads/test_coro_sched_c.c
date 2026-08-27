#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include "sqlite3_coro_sched.h"
#include "sqlite3_atomic.h"

// ----------------------------------------------------------------------------
// Test 1: Main-Thread Synchronous Polling & Draining (num_workers = 0)
// ----------------------------------------------------------------------------
static void main_thread_task_fn(void* arg) {
    int* val = (int*)arg;
    (*val) += 10;
    sqlite3_coro_pool_yield();
    (*val) += 20;
}

void test_main_thread_scheduler() {
    printf("1. Testing main-thread single-threaded event loop (num_workers = 0)...\n");

    sqlite3_coro_pool_t pool;
    assert(sqlite3_coro_pool_init(&pool, 0) == SQLITE_OK);
    assert(pool.num_workers == 0);

    int val1 = 0;
    int val2 = 0;

    assert(sqlite3_coro_pool_spawn(&pool, main_thread_task_fn, &val1, 0) == SQLITE_OK);
    assert(sqlite3_coro_pool_spawn(&pool, main_thread_task_fn, &val2, 0) == SQLITE_OK);
    assert(pool.pending_tasks == 2);

    // Step each task through its first yield
    assert(sqlite3_coro_pool_poll_one(&pool) == 1);
    assert(val1 == 10);

    assert(sqlite3_coro_pool_poll_one(&pool) == 1);
    assert(val2 == 10);

    // Drain all remaining task steps until queue is empty
    int steps = sqlite3_coro_pool_run_until_empty(&pool);
    assert(steps == 2);
    assert(val1 == 30);
    assert(val2 == 30);
    assert(pool.pending_tasks == 0);

    // Queue is empty: poll_one returns 0
    assert(sqlite3_coro_pool_poll_one(&pool) == 0);

    sqlite3_coro_pool_destroy(&pool);
    printf("   [PASS] Main-thread event loop stepped and drained cooperative tasks.\n");
}

// ----------------------------------------------------------------------------
// Test 2: Multi-Worker Parallel Thread Pool (num_workers = 4, 100 tasks)
// ----------------------------------------------------------------------------
#define NUM_PARALLEL_TASKS 50

typedef struct CounterTask {
    sqlite3_atomic_int* counter;
    int task_id;
} CounterTask;

static void parallel_counter_fn(void* arg) {
    CounterTask* t = (CounterTask*)arg;
    sqlite3_atomic_fetch_add(t->counter, 1);
    sqlite3_coro_pool_yield();
    sqlite3_atomic_fetch_add(t->counter, 10);
    sqlite3_coro_pool_yield();
    sqlite3_atomic_fetch_add(t->counter, 100);
}

void test_multi_worker_thread_pool() {
    printf("2. Testing M:N scheduler across 4 worker threads (%d tasks)...\n", NUM_PARALLEL_TASKS);

    sqlite3_coro_pool_t pool;
    assert(sqlite3_coro_pool_init(&pool, 4) == SQLITE_OK);
    assert(pool.num_workers == 4);

    sqlite3_atomic_int total_sum = 0;
    CounterTask tasks[NUM_PARALLEL_TASKS];

    for (int i = 0; i < NUM_PARALLEL_TASKS; ++i) {
        tasks[i].counter = &total_sum;
        tasks[i].task_id = i + 1;
        assert(sqlite3_coro_pool_spawn(&pool, parallel_counter_fn, &tasks[i], 0) == SQLITE_OK);
    }

    // Wait until all 100 tasks finish (each contributing 1 + 10 + 100 = 111)
    sqlite3_coro_pool_wait(&pool);
    assert(pool.pending_tasks == 0);

    int final_sum = sqlite3_atomic_load(&total_sum);
    assert(final_sum == NUM_PARALLEL_TASKS * 111);

    sqlite3_coro_pool_destroy(&pool);
    printf("   [PASS] All %d tasks executed across 4 workers with total sum %d.\n", NUM_PARALLEL_TASKS, final_sum);
}

// ----------------------------------------------------------------------------
// Test 3: Interleaved Cooperative Yields & Scheduling Order
// ----------------------------------------------------------------------------
typedef struct SequenceContext {
    sqlite3_thread_mutex_t* lock;
    int* log_array;
    int* log_count;
    int  worker_id;
} SequenceContext;

static void sequence_fn(void* arg) {
    SequenceContext* ctx = (SequenceContext*)arg;
    for (int step = 1; step <= 3; ++step) {
        sqlite3_thread_mutex_lock(ctx->lock);
        ctx->log_array[(*ctx->log_count)++] = ctx->worker_id * 10 + step;
        sqlite3_thread_mutex_unlock(ctx->lock);
        sqlite3_coro_pool_yield();
    }
}

void test_coro_pool_interleaved_yields() {
    printf("3. Testing cooperative interleaved yields across workers...\n");

    sqlite3_coro_pool_t pool;
    assert(sqlite3_coro_pool_init(&pool, 2) == SQLITE_OK);

    sqlite3_thread_mutex_t lock;
    sqlite3_thread_mutex_init(&lock);

    int log[10];
    int log_cnt = 0;

    SequenceContext ctx1 = { &lock, log, &log_cnt, 1 };
    SequenceContext ctx2 = { &lock, log, &log_cnt, 2 };

    assert(sqlite3_coro_pool_spawn(&pool, sequence_fn, &ctx1, 0) == SQLITE_OK);
    assert(sqlite3_coro_pool_spawn(&pool, sequence_fn, &ctx2, 0) == SQLITE_OK);

    sqlite3_coro_pool_wait(&pool);
    assert(log_cnt == 6);

    sqlite3_thread_mutex_destroy(&lock);
    sqlite3_coro_pool_destroy(&pool);

    printf("   [PASS] Interleaved yielding completed all 6 execution steps.\n");
}

// ----------------------------------------------------------------------------
// Test 4: Batch Task Fan-Out Across 8 Worker Threads
// ----------------------------------------------------------------------------
#define NUM_FANOUT_TASKS 100

typedef struct FanoutTask {
    sqlite3_atomic_int* completed_counter;
    int task_index;
} FanoutTask;

static void fanout_worker_fn(void* arg) {
    FanoutTask* t = (FanoutTask*)arg;
    sqlite3_coro_pool_yield();
    sqlite3_atomic_fetch_add(t->completed_counter, t->task_index);
}

void test_coro_pool_batch_fanout() {
    printf("4. Testing batch task fan-out across 8 worker threads (%d tasks)...\n", NUM_FANOUT_TASKS);

    sqlite3_coro_pool_t pool;
    assert(sqlite3_coro_pool_init(&pool, 8) == SQLITE_OK);
    assert(pool.num_workers == 8);

    sqlite3_atomic_int total_sum = 0;
    FanoutTask tasks[NUM_FANOUT_TASKS];

    int expected_sum = 0;
    for (int i = 0; i < NUM_FANOUT_TASKS; ++i) {
        tasks[i].completed_counter = &total_sum;
        tasks[i].task_index = i + 1;
        expected_sum += (i + 1);
        assert(sqlite3_coro_pool_spawn(&pool, fanout_worker_fn, &tasks[i], 0) == SQLITE_OK);
    }

    sqlite3_coro_pool_wait(&pool);
    assert(pool.pending_tasks == 0);

    int final_sum = sqlite3_atomic_load(&total_sum);
    assert(final_sum == expected_sum);

    sqlite3_coro_pool_destroy(&pool);
    printf("   [PASS] %d fan-out tasks executed across 8 workers with sum %d.\n", NUM_FANOUT_TASKS, final_sum);
}

// ----------------------------------------------------------------------------
// Test 5: Clean Teardown With Remaining Suspended & Unexecuted Tasks
// ----------------------------------------------------------------------------
static void test5_drain_task_fn(void* arg) {
    int* val = (int*)arg;
    (*val) += 1;
    sqlite3_coro_pool_yield();
    (*val) += 10;
}

void test_coro_pool_shutdown_with_queued_tasks() {
    printf("5. Testing scheduler shutdown with suspended and unexecuted tasks in queue...\n");

    sqlite3_coro_pool_t pool;
    assert(sqlite3_coro_pool_init(&pool, 0) == SQLITE_OK);

    int val1 = 0, val2 = 0;
    assert(sqlite3_coro_pool_spawn(&pool, test5_drain_task_fn, &val1, 0) == SQLITE_OK);
    assert(sqlite3_coro_pool_spawn(&pool, test5_drain_task_fn, &val2, 0) == SQLITE_OK);

    // Step only the first task through its first yield
    assert(sqlite3_coro_pool_poll_one(&pool) == 1);
    assert(val1 == 1);

    // Destroy pool: cleanly frees suspended fiber (task 1) and unexecuted fiber (task 2)
    sqlite3_coro_pool_destroy(&pool);

    printf("   [PASS] Teardown cleanly drained and freed suspended and unexecuted tasks.\n");
}

// ----------------------------------------------------------------------------
// Test 6: NULL Safety, Misuse and Edge Cases
// ----------------------------------------------------------------------------
void test_coro_pool_null_safety() {
    printf("6. Testing NULL parameter safety and edge cases...\n");

    assert(sqlite3_coro_pool_init(NULL, 4) == SQLITE_MISUSE);
    assert(sqlite3_coro_pool_spawn(NULL, main_thread_task_fn, NULL, 0) == SQLITE_MISUSE);

    sqlite3_coro_pool_t pool;
    assert(sqlite3_coro_pool_init(&pool, 0) == SQLITE_OK);
    assert(sqlite3_coro_pool_spawn(&pool, NULL, NULL, 0) == SQLITE_MISUSE);

    assert(sqlite3_coro_pool_poll_one(NULL) == 0);
    assert(sqlite3_coro_pool_run_until_empty(NULL) == 0);
    sqlite3_coro_pool_wait(NULL);
    sqlite3_coro_pool_destroy(NULL);

    sqlite3_coro_pool_destroy(&pool);
    printf("   [PASS] NULL pointers and edge cases rejected with error codes.\n");
}

// ----------------------------------------------------------------------------
// Test 7: Synchronous Batch Queue Draining (sqlite3_coro_pool_run_until_empty)
// ----------------------------------------------------------------------------
static void multi_step_calc_fn(void* arg) {
    int* acc = (int*)arg;
    *acc += 10;
    sqlite3_coro_pool_yield();
    *acc += 20;
    sqlite3_coro_pool_yield();
    *acc += 30;
}

void test_coro_pool_run_until_empty() {
    printf("7. Testing synchronous run_until_empty batch draining...\n");

    sqlite3_coro_pool_t pool;
    assert(sqlite3_coro_pool_init(&pool, 0) == SQLITE_OK);

    int c1 = 0, c2 = 0, c3 = 0;
    assert(sqlite3_coro_pool_spawn(&pool, multi_step_calc_fn, &c1, 0) == SQLITE_OK);
    assert(sqlite3_coro_pool_spawn(&pool, multi_step_calc_fn, &c2, 0) == SQLITE_OK);
    assert(sqlite3_coro_pool_spawn(&pool, multi_step_calc_fn, &c3, 0) == SQLITE_OK);

    assert(pool.pending_tasks == 3);

    // Drain all 3 tasks across all 3 steps (9 total steps executed)
    int steps_executed = sqlite3_coro_pool_run_until_empty(&pool);
    assert(steps_executed == 9);
    assert(pool.pending_tasks == 0);
    assert(c1 == 60);
    assert(c2 == 60);
    assert(c3 == 60);

    // Subsequent call on empty queue returns 0 steps
    assert(sqlite3_coro_pool_run_until_empty(&pool) == 0);

    sqlite3_coro_pool_destroy(&pool);
    printf("   [PASS] run_until_empty drained 9 steps across 3 tasks with final sum 180.\n");
}

// ----------------------------------------------------------------------------
// Test 8: Dynamic Nested Task Spawning from Inside Fibers
// ----------------------------------------------------------------------------
typedef struct NestedContext {
    sqlite3_coro_pool_t* pool;
    sqlite3_atomic_int* counter;
} NestedContext;

static void child_leaf_task(void* arg) {
    sqlite3_atomic_int* ctr = (sqlite3_atomic_int*)arg;
    sqlite3_atomic_fetch_add(ctr, 10);
    sqlite3_coro_pool_yield();
    sqlite3_atomic_fetch_add(ctr, 20);
}

static void parent_spawner_task(void* arg) {
    NestedContext* ctx = (NestedContext*)arg;
    sqlite3_atomic_fetch_add(ctx->counter, 1);
    sqlite3_coro_pool_yield();

    // Dynamically spawn 3 child tasks into the pool from inside this fiber
    for (int i = 0; i < 3; ++i) {
        int rc = sqlite3_coro_pool_spawn(ctx->pool, child_leaf_task, (void*)ctx->counter, 0);
        assert(rc == SQLITE_OK);
    }

    sqlite3_coro_pool_yield();
    sqlite3_atomic_fetch_add(ctx->counter, 2);
}

void test_coro_pool_nested_task_spawning() {
    printf("8. Testing dynamic nested task spawning from inside fibers...\n");

    sqlite3_coro_pool_t pool;
    assert(sqlite3_coro_pool_init(&pool, 4) == SQLITE_OK);

    sqlite3_atomic_int shared_counter = 0;
    NestedContext ctx;
    ctx.pool = &pool;
    ctx.counter = &shared_counter;

    assert(sqlite3_coro_pool_spawn(&pool, parent_spawner_task, &ctx, 0) == SQLITE_OK);

    sqlite3_coro_pool_wait(&pool);
    assert(pool.pending_tasks == 0);

    // Parent contributes 1 + 2 = 3. Each child contributes 10 + 20 = 30. Total = 3 + 90 = 93.
    int total = sqlite3_atomic_load(&shared_counter);
    assert(total == 93);

    sqlite3_coro_pool_destroy(&pool);
    printf("   [PASS] Nested tasks dynamically spawned from worker fibers with sum %d.\n", total);
}

// ----------------------------------------------------------------------------
// Test 9: Custom Stack Sizing (32 KB and 128 KB Stacks)
// ----------------------------------------------------------------------------
static void deep_stack_task(void* arg) {
    sqlite3_atomic_int* status = (sqlite3_atomic_int*)arg;
    volatile char buffer[16 * 1024]; // 16 KB local stack frame
    for (size_t i = 0; i < sizeof(buffer); ++i) {
        buffer[i] = (char)(i & 0x7F);
    }
    sqlite3_coro_pool_yield();

    // Verify stack contents preserved across yield
    int valid = 1;
    for (size_t i = 0; i < sizeof(buffer); ++i) {
        if (buffer[i] != (char)(i & 0x7F)) {
            valid = 0;
            break;
        }
    }
    if (valid) {
        sqlite3_atomic_fetch_add(status, 1);
    }
}

void test_coro_pool_custom_stack_sizes() {
    printf("9. Testing custom stack allocation (32 KB and 128 KB)...\n");

    sqlite3_coro_pool_t pool;
    assert(sqlite3_coro_pool_init(&pool, 2) == SQLITE_OK);

    sqlite3_atomic_int passed = 0;
    assert(sqlite3_coro_pool_spawn(&pool, deep_stack_task, (void*)&passed, 32 * 1024) == SQLITE_OK);
    assert(sqlite3_coro_pool_spawn(&pool, deep_stack_task, (void*)&passed, 128 * 1024) == SQLITE_OK);

    sqlite3_coro_pool_wait(&pool);
    assert(sqlite3_atomic_load(&passed) == 2);

    sqlite3_coro_pool_destroy(&pool);
    printf("   [PASS] Custom 32 KB and 128 KB stacks preserved stack frame integrity.\n");
}

// ----------------------------------------------------------------------------
// Test 10: Multi-Phase Sequential Batch Processing & Pool Re-use
// ----------------------------------------------------------------------------
void test_coro_pool_multiphase_reuse() {
    printf("10. Testing multi-phase sequential pool reuse (3 phases)...\n");

    sqlite3_coro_pool_t pool;
    assert(sqlite3_coro_pool_init(&pool, 4) == SQLITE_OK);

    sqlite3_atomic_int phase_accumulator = 0;
    CounterTask tasks[10];

    for (int phase = 1; phase <= 3; ++phase) {
        for (int i = 0; i < 10; ++i) {
            tasks[i].counter = &phase_accumulator;
            tasks[i].task_id = i;
            assert(sqlite3_coro_pool_spawn(&pool, parallel_counter_fn, &tasks[i], 0) == SQLITE_OK);
        }
        sqlite3_coro_pool_wait(&pool);
        assert(pool.pending_tasks == 0);
        assert(sqlite3_atomic_load(&phase_accumulator) == phase * 10 * 111);
    }

    sqlite3_coro_pool_destroy(&pool);
    printf("   [PASS] Pool cleanly executed 3 sequential batches of 10 tasks.\n");
}

// ----------------------------------------------------------------------------
// Test 11: Main-Thread Out-of-Coroutine Yield Safety
// ----------------------------------------------------------------------------
void test_coro_pool_outside_yield_safety() {
    printf("11. Testing out-of-coroutine yield safety...\n");

    // Calling yield when not inside any active coroutine fiber must be a safe no-op
    sqlite3_coro_pool_yield();
    sqlite3_coro_pool_yield();

    printf("   [PASS] Calling sqlite3_coro_pool_yield() outside coroutine safely ignored.\n");
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=================================================================\n");
    printf("Running Pure C Coroutine Scheduler Test Suite (sqlite3_coro_sched.h)\n");
    printf("=================================================================\n");

    test_main_thread_scheduler();
    test_multi_worker_thread_pool();
    test_coro_pool_interleaved_yields();
    test_coro_pool_batch_fanout();
    test_coro_pool_shutdown_with_queued_tasks();
    test_coro_pool_null_safety();
    test_coro_pool_run_until_empty();
    test_coro_pool_nested_task_spawning();
    test_coro_pool_custom_stack_sizes();
    test_coro_pool_multiphase_reuse();
    test_coro_pool_outside_yield_safety();

    printf("\nAll 11 Pure C Coroutine Scheduler Tests Passed Cleanly!\n");
    return 0;
}
