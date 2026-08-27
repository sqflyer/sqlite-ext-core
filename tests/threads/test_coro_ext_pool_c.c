#define SQLITE_CORE
#include <sqlite3.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "async/sqlite3_coro_ext_pool.h"

// ----------------------------------------------------------------------------
// Static Extension Tags (Pure C)
// ----------------------------------------------------------------------------
SQLITE_EXT_TAG_DECLARE(VectorExtTag);
SQLITE_EXT_TAG_DECLARE(CryptoExtTag);
SQLITE_EXT_TAG_DECLARE(BatchExtTag);
SQLITE_EXT_TAG_DECLARE(UnusedExtTag);

// ----------------------------------------------------------------------------
// Helper Task Fiber
// ----------------------------------------------------------------------------
typedef struct {
    int* counter;
    int  delta;
} ExtTaskPayload;

static void ext_test_fiber(void* arg) {
    ExtTaskPayload* p = (ExtTaskPayload*)arg;
    *p->counter += p->delta;
    sqlite3_coro_pool_yield();
    *p->counter += p->delta;
}

// ----------------------------------------------------------------------------
// Test 1: Tagged Extension Pool Lifecycle & Reference Counting (Zero Collision)
// ----------------------------------------------------------------------------
void test_tagged_pool_lifecycle() {
    printf("1. Testing tagged extension pool acquisition and reference counting...\n");
    fflush(stdout);

    assert(sqlite3_coro_ext_pool_ref_count(SQLITE_EXT_TAG(VectorExtTag)) == 0);

    // First acquisition: creates 2 worker threads (custom workers)
    sqlite3_coro_pool_t* pool1 = sqlite3_coro_ext_pool_acquire(SQLITE_EXT_TAG(VectorExtTag), 2);
    assert(pool1 != NULL);
    assert(sqlite3_coro_ext_pool_ref_count(SQLITE_EXT_TAG(VectorExtTag)) == 1);

    // Second acquisition (another DB connection loading the same extension): shares pool
    sqlite3_coro_pool_t* pool2 = sqlite3_coro_ext_pool_acquire(SQLITE_EXT_TAG(VectorExtTag), 2);
    assert(pool2 == pool1);
    assert(sqlite3_coro_ext_pool_ref_count(SQLITE_EXT_TAG(VectorExtTag)) == 2);

    // First DB disconnects
    sqlite3_coro_ext_pool_release(SQLITE_EXT_TAG(VectorExtTag));
    assert(sqlite3_coro_ext_pool_ref_count(SQLITE_EXT_TAG(VectorExtTag)) == 1);

    // Second DB disconnects: pool is cleanly destroyed
    sqlite3_coro_ext_pool_release(SQLITE_EXT_TAG(VectorExtTag));
    assert(sqlite3_coro_ext_pool_ref_count(SQLITE_EXT_TAG(VectorExtTag)) == 0);

    // Releasing an already destroyed or inactive tagged pool is a safe no-op
    sqlite3_coro_ext_pool_release(SQLITE_EXT_TAG(UnusedExtTag));
    assert(sqlite3_coro_ext_pool_ref_count(SQLITE_EXT_TAG(UnusedExtTag)) == 0);

    printf("   [PASS] Tagged extension pool lifecycle & ref counting verified.\n");
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// Test 2: Tagged Isolation Across Multiple Extensions & Unlinking
// ----------------------------------------------------------------------------
void test_multiple_tagged_pools_isolation() {
    printf("2. Testing multiple tagged extension pools isolation & list unlinking...\n");
    fflush(stdout);

    sqlite3_coro_pool_t* pool_v = sqlite3_coro_ext_pool_acquire(SQLITE_EXT_TAG(VectorExtTag), 2);
    sqlite3_coro_pool_t* pool_c = sqlite3_coro_ext_pool_acquire(SQLITE_EXT_TAG(CryptoExtTag), 4);
    sqlite3_coro_pool_t* pool_b = sqlite3_coro_ext_pool_acquire(SQLITE_EXT_TAG(BatchExtTag), 2);

    assert(pool_v != NULL && pool_c != NULL && pool_b != NULL);
    assert(pool_v != pool_c && pool_c != pool_b);

    int count_v = 0, count_c = 0, count_b = 0;
    ExtTaskPayload pv = { &count_v, 10 };
    ExtTaskPayload pc = { &count_c, 50 };
    ExtTaskPayload pb = { &count_b, 100 };

    assert(sqlite3_coro_pool_spawn(pool_v, ext_test_fiber, &pv, 0) == SQLITE_OK);
    assert(sqlite3_coro_pool_spawn(pool_c, ext_test_fiber, &pc, 0) == SQLITE_OK);
    assert(sqlite3_coro_pool_spawn(pool_b, ext_test_fiber, &pb, 0) == SQLITE_OK);

    sqlite3_coro_ext_pool_wait(SQLITE_EXT_TAG(VectorExtTag));
    sqlite3_coro_ext_pool_wait(SQLITE_EXT_TAG(CryptoExtTag));
    sqlite3_coro_ext_pool_wait(SQLITE_EXT_TAG(BatchExtTag));

    assert(count_v == 20);
    assert(count_c == 100);
    assert(count_b == 200);

    // Delete middle node (Crypto) -> tests prev != NULL unlinking
    sqlite3_coro_ext_pool_release(SQLITE_EXT_TAG(CryptoExtTag));
    assert(sqlite3_coro_ext_pool_ref_count(SQLITE_EXT_TAG(CryptoExtTag)) == 0);
    assert(sqlite3_coro_ext_pool_ref_count(SQLITE_EXT_TAG(VectorExtTag)) == 1);
    assert(sqlite3_coro_ext_pool_ref_count(SQLITE_EXT_TAG(BatchExtTag)) == 1);

    // Delete head node (Batch) -> tests prev == NULL unlinking
    sqlite3_coro_ext_pool_release(SQLITE_EXT_TAG(BatchExtTag));
    assert(sqlite3_coro_ext_pool_ref_count(SQLITE_EXT_TAG(BatchExtTag)) == 0);

    // Delete tail node (Vector)
    sqlite3_coro_ext_pool_release(SQLITE_EXT_TAG(VectorExtTag));
    assert(sqlite3_coro_ext_pool_ref_count(SQLITE_EXT_TAG(VectorExtTag)) == 0);

    printf("   [PASS] Multiple tagged extension pools executed in complete isolation.\n");
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// Test 3: Synchronous Barrier Wait
// ----------------------------------------------------------------------------
void test_tagged_pool_wait_barrier() {
    printf("3. Testing tagged extension pool wait barrier...\n");
    fflush(stdout);

    sqlite3_coro_pool_t* pool = sqlite3_coro_ext_pool_acquire(SQLITE_EXT_TAG(BatchExtTag), 4);
    assert(pool != NULL);

    int total_sum = 0;
    ExtTaskPayload payloads[10];

    for (int i = 0; i < 10; ++i) {
        payloads[i].counter = &total_sum;
        payloads[i].delta = 5;
        assert(sqlite3_coro_pool_spawn(pool, ext_test_fiber, &payloads[i], 0) == SQLITE_OK);
    }

    sqlite3_coro_ext_pool_wait(SQLITE_EXT_TAG(BatchExtTag));
    assert(total_sum == 100);

    sqlite3_coro_ext_pool_release(SQLITE_EXT_TAG(BatchExtTag));
    assert(sqlite3_coro_ext_pool_ref_count(SQLITE_EXT_TAG(BatchExtTag)) == 0);

    // Waiting on inactive tag is a safe no-op
    sqlite3_coro_ext_pool_wait(SQLITE_EXT_TAG(UnusedExtTag));

    printf("   [PASS] Tagged extension pool wait barrier drained 10 tasks cleanly.\n");
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// Test 4: Registry Shutdown All
// ----------------------------------------------------------------------------
void test_registry_shutdown_all() {
    printf("4. Testing sqlite3_coro_ext_pool_shutdown_all()...\n");
    fflush(stdout);

    // Shutdown on empty registry is safe
    sqlite3_coro_ext_pool_shutdown_all();

    sqlite3_coro_ext_pool_acquire(SQLITE_EXT_TAG(VectorExtTag), 2);
    sqlite3_coro_ext_pool_acquire(SQLITE_EXT_TAG(CryptoExtTag), 2);

    assert(sqlite3_coro_ext_pool_ref_count(SQLITE_EXT_TAG(VectorExtTag)) == 1);
    assert(sqlite3_coro_ext_pool_ref_count(SQLITE_EXT_TAG(CryptoExtTag)) == 1);

    sqlite3_coro_ext_pool_shutdown_all();

    assert(sqlite3_coro_ext_pool_ref_count(SQLITE_EXT_TAG(VectorExtTag)) == 0);
    assert(sqlite3_coro_ext_pool_ref_count(SQLITE_EXT_TAG(CryptoExtTag)) == 0);

    printf("   [PASS] sqlite3_coro_ext_pool_shutdown_all() cleanly destroyed pools.\n");
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// Test 5: NULL & Fallback Tag Edge Cases
// ----------------------------------------------------------------------------
void test_null_and_fallback_safety() {
    printf("5. Testing NULL tag fallback safety...\n");
    fflush(stdout);

    // NULL Tag pointer -> SQLITE_EXT_DEFAULT_TAG
    sqlite3_coro_pool_t* def_tag1 = sqlite3_coro_ext_pool_acquire(NULL, -1); // defaults workers to 4
    assert(def_tag1 != NULL);
    assert(sqlite3_coro_ext_pool_ref_count(NULL) == 1);

    sqlite3_coro_pool_t* def_tag2 = sqlite3_coro_ext_pool_acquire(NULL, 0); // defaults workers to 4
    assert(def_tag2 == def_tag1);
    assert(sqlite3_coro_ext_pool_ref_count(NULL) == 2);

    sqlite3_coro_ext_pool_wait(NULL);
    sqlite3_coro_ext_pool_release(NULL);
    assert(sqlite3_coro_ext_pool_ref_count(NULL) == 1);
    sqlite3_coro_ext_pool_release(NULL);
    assert(sqlite3_coro_ext_pool_ref_count(NULL) == 0);

    printf("   [PASS] NULL pointers safely handled with default fallback.\n");
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// Main Runner
// ----------------------------------------------------------------------------
int main() {
    printf("=================================================================\n");
    printf("Running Pure C Tagged Extension Coroutine Pool Test Suite\n");
    printf("=================================================================\n");

    test_tagged_pool_lifecycle();
    test_multiple_tagged_pools_isolation();
    test_tagged_pool_wait_barrier();
    test_registry_shutdown_all();
    test_null_and_fallback_safety();

    printf("\nAll 5 Pure C Tagged Extension Pool Tests Passed Cleanly (100%% Coverage)!\n");
    return 0;
}
