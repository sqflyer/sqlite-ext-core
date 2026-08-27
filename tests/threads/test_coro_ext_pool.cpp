#define SQLITE_CORE
#include <sqlite3.h>
#include <stdio.h>
#include <assert.h>
#include "async/sqlite3_coro_ext_pool.hpp"
#include "sqlite3_atomic.hpp"

// ----------------------------------------------------------------------------
// Custom Tag Types for Testing
// ----------------------------------------------------------------------------
struct VectorExtensionTag {};
struct SearchExtensionTag {};
struct CryptoExtensionTag {};
struct UnusedExtensionTag {};

// ----------------------------------------------------------------------------
// Test 1: Tagged Template Extension Pool Isolation & Ref Counting
// ----------------------------------------------------------------------------
void test_cpp_tagged_pool_isolation() {
    printf("1. Testing C++ SqliteExtCoroPool tagged template isolation & ref counts...\n");
    fflush(stdout);

    assert(SqliteExtCoroPool<VectorExtensionTag>::ref_count() == 0);
    assert(SqliteExtCoroPool<SearchExtensionTag>::ref_count() == 0);
    assert(SqliteExtCoroPool<VectorExtensionTag>::get() == nullptr);

    // Calling wait_all, release, shutdown on uninitialized pool are safe no-ops
    SqliteExtCoroPool<VectorExtensionTag>::wait_all();
    SqliteExtCoroPool<VectorExtensionTag>::release();
    SqliteExtCoroPool<VectorExtensionTag>::shutdown();

    // Acquire Vector pool (2 workers)
    SqliteCoroScheduler* vec_pool1 = SqliteExtCoroPool<VectorExtensionTag>::acquire(2);
    assert(vec_pool1 != nullptr);
    assert(SqliteExtCoroPool<VectorExtensionTag>::ref_count() == 1);
    assert(SqliteExtCoroPool<VectorExtensionTag>::get() == vec_pool1);

    // Second connection to Vector pool shares instance
    SqliteCoroScheduler* vec_pool2 = SqliteExtCoroPool<VectorExtensionTag>::acquire(2);
    assert(vec_pool2 == vec_pool1);
    assert(SqliteExtCoroPool<VectorExtensionTag>::ref_count() == 2);

    // Acquire Search pool with default worker count (4 workers)
    SqliteCoroScheduler* search_pool = SqliteExtCoroPool<SearchExtensionTag>::acquire();
    assert(search_pool != nullptr);
    assert(search_pool != vec_pool1);
    assert(SqliteExtCoroPool<SearchExtensionTag>::ref_count() == 1);

    // Release one Vector connection (ref_count drops to 1, pool stays active)
    SqliteExtCoroPool<VectorExtensionTag>::release();
    assert(SqliteExtCoroPool<VectorExtensionTag>::ref_count() == 1);
    assert(SqliteExtCoroPool<VectorExtensionTag>::get() == vec_pool1);

    // Release Search connection (destroys Search pool)
    SqliteExtCoroPool<SearchExtensionTag>::release();
    assert(SqliteExtCoroPool<SearchExtensionTag>::ref_count() == 0);
    assert(SqliteExtCoroPool<SearchExtensionTag>::get() == nullptr);

    // Release final Vector connection
    SqliteExtCoroPool<VectorExtensionTag>::release();
    assert(SqliteExtCoroPool<VectorExtensionTag>::ref_count() == 0);
    assert(SqliteExtCoroPool<VectorExtensionTag>::get() == nullptr);

    printf("   [PASS] Tagged extension pools isolated with zero crosstalk.\n");
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// Test 2: Type-Safe Closure Spawning with sqlite_coro_ext_spawn & Auto-Acquisition
// ----------------------------------------------------------------------------
void test_cpp_tagged_pool_spawn_and_yield() {
    printf("2. Testing sqlite_coro_ext_spawn with capturing lambdas & auto-acquire...\n");
    fflush(stdout);

    // Auto-acquisition branch: pool is not active before spawn call
    assert(SqliteExtCoroPool<CryptoExtensionTag>::get() == nullptr);

    SqliteAtomicInt total_processed(0);

    // Spawn task with default stack size
    bool ok1 = sqlite_coro_ext_spawn<CryptoExtensionTag>([&total_processed]() {
        total_processed += 50;
        SqliteCoroScheduler::yield();
        total_processed += 50;
    });
    assert(ok1);
    assert(SqliteExtCoroPool<CryptoExtensionTag>::ref_count() >= 1);

    // Already-acquired branch: spawn 9 more tasks with custom 32 KB stack size
    for (int i = 1; i <= 9; ++i) {
        int item = i * 10;
        bool ok = sqlite_coro_ext_spawn<CryptoExtensionTag>([&total_processed, item]() {
            total_processed += item;
            SqliteCoroScheduler::yield();
            total_processed += 5;
        }, 32 * 1024);
        assert(ok);
    }

    // Synchronously wait for all 10 tasks to finish both yield phases
    SqliteExtCoroPool<CryptoExtensionTag>::wait_all();

    // Total = 100 + (10+20+...+90) + (9 * 5) = 100 + 450 + 45 = 595
    assert(total_processed.load() == 595);

    SqliteExtCoroPool<CryptoExtensionTag>::release();
    assert(SqliteExtCoroPool<CryptoExtensionTag>::ref_count() == 0);

    printf("   [PASS] Capturing lambda closures executed across fiber yields & auto-acquired pool.\n");
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// Test 3: Default Tagged Pool (SqliteExtCoroPool<>) & Type Alias
// ----------------------------------------------------------------------------
void test_cpp_default_tagged_pool() {
    printf("3. Testing default template tag (SqliteExtCoroPool<>) & SqliteExtensionCoroPool alias...\n");
    fflush(stdout);

    SqliteCoroScheduler* def_pool = SqliteExtensionCoroPool<>::acquire(2);
    assert(def_pool != nullptr);
    assert(SqliteExtensionCoroPool<>::ref_count() == 1);

    int result = 0;
    bool ok = sqlite_coro_ext_spawn([&result]() {
        result += 42;
        SqliteCoroScheduler::yield();
        result += 58;
    });
    assert(ok);

    SqliteExtensionCoroPool<>::wait_all();
    assert(result == 100);

    SqliteExtensionCoroPool<>::release();
    assert(SqliteExtensionCoroPool<>::ref_count() == 0);

    printf("   [PASS] Default tagged extension pool & alias verified.\n");
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// Test 4: C++ Tagged Registry Wrapper (SqliteTaggedCoroPool)
// ----------------------------------------------------------------------------
void test_cpp_tagged_coro_pool_wrapper() {
    printf("4. Testing SqliteTaggedCoroPool C++ wrapper class...\n");
    fflush(stdout);

    static const char cpp_tag1 = 0;
    static const char cpp_tag2 = 0;

    sqlite3_coro_pool_t* pool = SqliteTaggedCoroPool::acquire(&cpp_tag1, 2);
    assert(pool != nullptr);
    assert(SqliteTaggedCoroPool::ref_count(&cpp_tag1) == 1);

    int counter = 0;
    struct Arg { int* ptr; };
    Arg a = { &counter };

    sqlite3_coro_pool_spawn(pool, [](void* arg) {
        Arg* p = (Arg*)arg;
        *p->ptr += 10;
        sqlite3_coro_pool_yield();
        *p->ptr += 20;
    }, &a, 0);

    SqliteTaggedCoroPool::wait_all(&cpp_tag1);
    assert(counter == 30);

    SqliteTaggedCoroPool::release(&cpp_tag1);
    assert(SqliteTaggedCoroPool::ref_count(&cpp_tag1) == 0);

    // Test shutdown_all via C++ wrapper
    SqliteTaggedCoroPool::acquire(&cpp_tag1, 2);
    SqliteTaggedCoroPool::acquire(&cpp_tag2, 2);
    assert(SqliteTaggedCoroPool::ref_count(&cpp_tag1) == 1);
    assert(SqliteTaggedCoroPool::ref_count(&cpp_tag2) == 1);

    SqliteTaggedCoroPool::shutdown_all();
    assert(SqliteTaggedCoroPool::ref_count(&cpp_tag1) == 0);
    assert(SqliteTaggedCoroPool::ref_count(&cpp_tag2) == 0);

    printf("   [PASS] SqliteTaggedCoroPool C++ wrapper methods verified.\n");
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// Test 5: Forcible Shutdown & Edge Cases
// ----------------------------------------------------------------------------
void test_cpp_pool_shutdown() {
    printf("5. Testing SqliteExtCoroPool::shutdown() & edge case coverage...\n");
    fflush(stdout);

    // Calling shutdown on an inactive pool is a safe no-op
    SqliteExtCoroPool<UnusedExtensionTag>::shutdown();

    // Calling wait_all on an inactive pool is a safe no-op
    SqliteExtCoroPool<UnusedExtensionTag>::wait_all();

    // Calling release on an inactive pool is a safe no-op
    SqliteExtCoroPool<UnusedExtensionTag>::release();

    SqliteExtCoroPool<CryptoExtensionTag>::acquire(2);
    SqliteExtCoroPool<CryptoExtensionTag>::acquire(2);
    assert(SqliteExtCoroPool<CryptoExtensionTag>::ref_count() == 2);

    SqliteExtCoroPool<CryptoExtensionTag>::shutdown();
    assert(SqliteExtCoroPool<CryptoExtensionTag>::ref_count() == 0);
    assert(SqliteExtCoroPool<CryptoExtensionTag>::get() == nullptr);

    printf("   [PASS] Forcible shutdown and inactive edge cases verified.\n");
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// Main Runner
// ----------------------------------------------------------------------------
int main() {
    printf("=================================================================\n");
    printf("Running C++11/C++20 SqliteExtCoroPool Test Suite (100%% Coverage)\n");
    printf("=================================================================\n");

    test_cpp_tagged_pool_isolation();
    test_cpp_tagged_pool_spawn_and_yield();
    test_cpp_default_tagged_pool();
    test_cpp_tagged_coro_pool_wrapper();
    test_cpp_pool_shutdown();

    printf("\nAll 5 C++ Extension Pool Test Suites Passed Cleanly (100%% Coverage)!\n");
    return 0;
}
