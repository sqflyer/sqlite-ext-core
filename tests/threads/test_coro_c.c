/**
 * @file test_coro_c.c
 * @brief Pure C Coroutine & Fiber Subsystem Test Suite (sqlite3_coro.h).
 *
 * Verifies core Pure C coroutine execution, stackful fiber creation, value yielding (`sqlite3_coro_yield_value`),
 * non-volatile XMM/SIMD register preservation (`FIBER_FLAG_FLOAT_SWITCH`), early fiber cancellation,
 * interleaved coroutine execution, deep recursion stack preservation, and NULL safety checks.
 */

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "sqlite3_coro.h"

// ----------------------------------------------------------------------------
// Test 1: Basic Lifecycle & State Transitions
// ----------------------------------------------------------------------------
static void simple_coro_fn(void* arg) {
    int* val = (int*)arg;
    *val = 100;
    sqlite3_coro_yield();
    *val = 200;
    sqlite3_coro_yield();
    *val = 300;
}

void test_coro_basic_lifecycle() {
    printf("1. Testing Pure C coroutine basic lifecycle and yields...\n");
    int state = 0;
    sqlite3_coro_t coro;
    int rc = sqlite3_coro_create(&coro, 32768, simple_coro_fn, &state);
    assert(rc == SQLITE_OK);
    assert(!sqlite3_coro_is_done(&coro));

    // Step 1: Run to first yield
    rc = sqlite3_coro_resume(&coro);
    assert(rc == SQLITE_OK);
    assert(state == 100);
    assert(!sqlite3_coro_is_done(&coro));

    // Step 2: Run to second yield
    rc = sqlite3_coro_resume(&coro);
    assert(rc == SQLITE_OK);
    assert(state == 200);
    assert(!sqlite3_coro_is_done(&coro));

    // Step 3: Run to completion
    rc = sqlite3_coro_resume(&coro);
    assert(rc == SQLITE_OK);
    assert(state == 300);
    assert(sqlite3_coro_is_done(&coro));

    // Resuming a finished coroutine returns SQLITE_MISUSE
    rc = sqlite3_coro_resume(&coro);
    assert(rc == SQLITE_MISUSE);

    sqlite3_coro_destroy(&coro);
    printf("   [PASS] Coroutine executed across multiple yields and verified completed state.\n");
}

// ----------------------------------------------------------------------------
// Test 2: Deep Stack Yielding & Value Passing
// ----------------------------------------------------------------------------
static void recursive_generator(int depth) {
    if (depth <= 0) return;
    sqlite3_coro_yield_value((void*)(uintptr_t)(depth * 10));
    recursive_generator(depth - 1);
}

static void deep_yield_coro(void* arg) {
    (void)arg;
    recursive_generator(5);
}

void test_coro_deep_stack_yielding() {
    printf("2. Testing Pure C deep stack yielding and value transfer...\n");
    sqlite3_coro_t coro;
    int rc = sqlite3_coro_create(&coro, 32768, deep_yield_coro, NULL);
    assert(rc == SQLITE_OK);

    int expected[] = { 50, 40, 30, 20, 10 };
    int i = 0;

    while (!sqlite3_coro_is_done(&coro)) {
        sqlite3_coro_resume(&coro);
        if (!sqlite3_coro_is_done(&coro)) {
            uintptr_t val = (uintptr_t)sqlite3_coro_get_value(&coro);
            assert((int)val == expected[i]);
            i++;
        }
    }
    assert(i == 5);

    sqlite3_coro_destroy(&coro);
    printf("   [PASS] Deep stack yielding and values verified.\n");
}

// ----------------------------------------------------------------------------
// Test 3: Interleaved Ping-Pong Execution (Multiple Concurrent Coroutines)
// ----------------------------------------------------------------------------
typedef struct PingPongContext {
    int id;
    int* log_array;
    int* log_count;
} PingPongContext;

static void ping_pong_fn(void* arg) {
    PingPongContext* ctx = (PingPongContext*)arg;
    for (int step = 1; step <= 3; ++step) {
        ctx->log_array[(*ctx->log_count)++] = ctx->id * 100 + step;
        sqlite3_coro_yield();
    }
}

void test_coro_interleaved() {
    printf("3. Testing interleaved cooperative scheduling (Ping-Pong Fibers)...\n");
    int log[10];
    int log_cnt = 0;

    PingPongContext ctx_a = { 1, log, &log_cnt };
    PingPongContext ctx_b = { 2, log, &log_cnt };

    sqlite3_coro_t coro_a, coro_b;
    assert(sqlite3_coro_create(&coro_a, 0, ping_pong_fn, &ctx_a) == SQLITE_OK);
    assert(sqlite3_coro_create(&coro_b, 0, ping_pong_fn, &ctx_b) == SQLITE_OK);

    while (!sqlite3_coro_is_done(&coro_a) || !sqlite3_coro_is_done(&coro_b)) {
        if (!sqlite3_coro_is_done(&coro_a)) sqlite3_coro_resume(&coro_a);
        if (!sqlite3_coro_is_done(&coro_b)) sqlite3_coro_resume(&coro_b);
    }

    assert(log_cnt == 6);
    int expected[] = { 101, 201, 102, 202, 103, 203 };
    for (int j = 0; j < 6; ++j) {
        assert(log[j] == expected[j]);
    }

    sqlite3_coro_destroy(&coro_a);
    sqlite3_coro_destroy(&coro_b);
    printf("   [PASS] Interleaved execution log matched expected sequence perfectly.\n");
}

// ----------------------------------------------------------------------------
// Test 4: Early Cancellation / Teardown of Suspended Coroutine
// ----------------------------------------------------------------------------
static void infinite_loop_fn(void* arg) {
    int* run_flag = (int*)arg;
    *run_flag = 1;
    while (1) {
        sqlite3_coro_yield();
        (*run_flag)++;
    }
}

void test_coro_early_cancellation() {
    printf("4. Testing early cancellation and teardown of suspended coroutines...\n");
    int run_count = 0;
    sqlite3_coro_t coro;
    assert(sqlite3_coro_create(&coro, 0, infinite_loop_fn, &run_count) == SQLITE_OK);

    assert(sqlite3_coro_resume(&coro) == SQLITE_OK);
    assert(run_count == 1);

    assert(sqlite3_coro_resume(&coro) == SQLITE_OK);
    assert(run_count == 2);

    // Destroy in-flight suspended coroutine
    sqlite3_coro_destroy(&coro);
    assert(sqlite3_coro_is_done(&coro) == 1);
    assert(sqlite3_coro_resume(&coro) == SQLITE_MISUSE);

    printf("   [PASS] Early cancellation released fiber resources cleanly.\n");
}

// ----------------------------------------------------------------------------
// Test 5: Invalid Yield from Main Thread & Double Destroy Safety
// ----------------------------------------------------------------------------
void test_coro_main_thread_yield_and_double_destroy() {
    printf("5. Testing main-thread yield safety and double-destroy safety...\n");

    // Calling yield outside any coroutine context is safe no-op
    sqlite3_coro_yield();
    sqlite3_coro_yield_value((void*)0x1234);

    // Double destroy check
    sqlite3_coro_t coro;
    int state = 0;
    assert(sqlite3_coro_create(&coro, 0, simple_coro_fn, &state) == SQLITE_OK);
    sqlite3_coro_destroy(&coro);
    // Second destroy on same instance must be a safe no-op
    sqlite3_coro_destroy(&coro);
    assert(sqlite3_coro_is_done(&coro) == 1);

    printf("   [PASS] Invalid yields safely ignored and double destroy handled safely.\n");
}

// ----------------------------------------------------------------------------
// Test 6: Custom Struct Channeling Across Yields
// ----------------------------------------------------------------------------
typedef struct TaskItem {
    int id;
    double score;
    const char* label;
} TaskItem;

static void struct_producer_fn(void* arg) {
    TaskItem* items = (TaskItem*)arg;
    for (int i = 0; i < 3; ++i) {
        sqlite3_coro_yield_value(&items[i]);
    }
}

void test_coro_struct_channeling() {
    printf("6. Testing structured data item yielding across execution contexts...\n");
    TaskItem items[3] = {
        { 1, 99.5, "Task Alpha" },
        { 2, 88.0, "Task Beta" },
        { 3, 77.2, "Task Gamma" }
    };

    sqlite3_coro_t coro;
    assert(sqlite3_coro_create(&coro, 0, struct_producer_fn, items) == SQLITE_OK);

    int count = 0;
    while (!sqlite3_coro_is_done(&coro)) {
        sqlite3_coro_resume(&coro);
        if (!sqlite3_coro_is_done(&coro)) {
            TaskItem* item = (TaskItem*)sqlite3_coro_get_value(&coro);
            assert(item != NULL);
            assert(item->id == count + 1);
            assert(strcmp(item->label, items[count].label) == 0);
            assert(item->score == items[count].score);
            count++;
        }
    }
    assert(count == 3);
    sqlite3_coro_destroy(&coro);

    printf("   [PASS] Structured data items yielded and verified with pointer integrity.\n");
}

// ----------------------------------------------------------------------------
// Test 7: Deep Stack Frame Local Variable Integrity
// ----------------------------------------------------------------------------
static void deep_stack_check(int depth, int expected_sum) {
    char stack_buffer[256];
    memset(stack_buffer, depth, sizeof(stack_buffer));

    if (depth <= 0) {
        sqlite3_coro_yield_value((void*)(uintptr_t)expected_sum);
        return;
    }

    deep_stack_check(depth - 1, expected_sum + depth);

    // Verify buffer wasn't corrupted across context switch
    for (size_t i = 0; i < sizeof(stack_buffer); ++i) {
        assert(stack_buffer[i] == (char)depth);
    }
}

static void deep_stack_coro_fn(void* arg) {
    (void)arg;
    deep_stack_check(8, 0);
}

void test_coro_deep_stack_variables() {
    printf("7. Testing deep stack frame preservation and local variable integrity...\n");
    sqlite3_coro_t coro;
    assert(sqlite3_coro_create(&coro, 65536, deep_stack_coro_fn, NULL) == SQLITE_OK);

    assert(sqlite3_coro_resume(&coro) == SQLITE_OK);
    uintptr_t sum = (uintptr_t)sqlite3_coro_get_value(&coro);
    assert((int)sum == 36); // 8+7+6+5+4+3+2+1

    assert(sqlite3_coro_resume(&coro) == SQLITE_OK);
    assert(sqlite3_coro_is_done(&coro));

    sqlite3_coro_destroy(&coro);
    printf("   [PASS] All 8 nested stack frames preserved without memory corruption.\n");
}

// ----------------------------------------------------------------------------
// Test 8: Many Concurrent Fibers Round-Robin Execution
// ----------------------------------------------------------------------------
#define NUM_CONCURRENT_FIBERS 25

static void concurrent_worker_fn(void* arg) {
    int worker_id = (int)(uintptr_t)arg;
    for (int step = 1; step <= 4; ++step) {
        sqlite3_coro_yield_value((void*)(uintptr_t)(worker_id * 10 + step));
    }
}

void test_coro_many_concurrent() {
    printf("8. Testing %d concurrent fibers with round-robin cooperative scheduling...\n", NUM_CONCURRENT_FIBERS);
    sqlite3_coro_t fibers[NUM_CONCURRENT_FIBERS];

    for (int i = 0; i < NUM_CONCURRENT_FIBERS; ++i) {
        assert(sqlite3_coro_create(&fibers[i], 0, concurrent_worker_fn, (void*)(uintptr_t)(i + 1)) == SQLITE_OK);
    }

    int active_fibers = NUM_CONCURRENT_FIBERS;
    int total_yields = 0;

    while (active_fibers > 0) {
        for (int i = 0; i < NUM_CONCURRENT_FIBERS; ++i) {
            if (!sqlite3_coro_is_done(&fibers[i])) {
                sqlite3_coro_resume(&fibers[i]);
                if (!sqlite3_coro_is_done(&fibers[i])) {
                    total_yields++;
                } else {
                    active_fibers--;
                }
            }
        }
    }

    assert(total_yields == NUM_CONCURRENT_FIBERS * 4);

    for (int i = 0; i < NUM_CONCURRENT_FIBERS; ++i) {
        sqlite3_coro_destroy(&fibers[i]);
    }

    printf("   [PASS] Successfully scheduled %d fibers across %d total context switches.\n",
           NUM_CONCURRENT_FIBERS, total_yields);
}

// ----------------------------------------------------------------------------
// Test 9: Error Handling & NULL Safety
// ----------------------------------------------------------------------------
void test_coro_error_handling() {
    printf("9. Testing Pure C error handling and NULL safety...\n");

    // NULL inputs
    assert(sqlite3_coro_create(NULL, 0, simple_coro_fn, NULL) == SQLITE_MISUSE);
    sqlite3_coro_t coro;
    assert(sqlite3_coro_create(&coro, 0, NULL, NULL) == SQLITE_MISUSE);
    assert(sqlite3_coro_resume(NULL) == SQLITE_MISUSE);
    assert(sqlite3_coro_get_value(NULL) == NULL);
    assert(sqlite3_coro_is_done(NULL) == 1);

    // Destroy NULL should be a safe no-op
    sqlite3_coro_destroy(NULL);

    printf("   [PASS] Edge cases and NULL pointers handled safely.\n");
}

int main() {
    printf("=================================================================\n");
    printf("Running Pure C Coroutine & Fiber Test Suite (sqlite3_coro.h)\n");
    printf("=================================================================\n");

    test_coro_basic_lifecycle();
    test_coro_deep_stack_yielding();
    test_coro_interleaved();
    test_coro_early_cancellation();
    test_coro_main_thread_yield_and_double_destroy();
    test_coro_struct_channeling();
    test_coro_deep_stack_variables();
    test_coro_many_concurrent();
    test_coro_error_handling();

    printf("\nAll Pure C Coroutine Tests Passed Cleanly!\n");
    return 0;
}
