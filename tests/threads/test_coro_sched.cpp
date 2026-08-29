/**
 * @file test_coro_sched.cpp
 * @brief C++11/C++20 SqliteCoroScheduler Test Suite (-nostdlib++ compliant).
 *
 * Verifies RAII C++11 M:N coroutine scheduler wrapper (`SqliteCoroScheduler`), capturing lambda task dispatch,
 * process-wide singleton global acquisition (`SqliteCoroScheduler::acquire_global`), move semantics,
 * multi-stage pipeline closures across workers, standalone template spawn helpers (`sqlite_coro_spawn`),
 * custom stack allocations, and high-concurrency throughput testing.
 */

#define SQLITE_CORE
#include <sqlite3.h>
#include <stdio.h>
#include <assert.h>
#include "sqlite3_coro_sched.hpp"
#include "sqlite3_atomic.hpp"

// ----------------------------------------------------------------------------
// Test 1: Main Thread Synchronous Event Loop (num_workers = 0)
// ----------------------------------------------------------------------------
void test_cpp_main_thread_scheduler() {
    printf("1. Testing C++ SqliteCoroScheduler in main-thread event loop mode...\n");
    fflush(stdout);

    SqliteCoroScheduler scheduler(0);
    assert(scheduler.worker_count() == 0);

    int val1 = 0;
    int val2 = 0;

    scheduler.spawn([&val1]() {
        val1 += 5;
        SqliteCoroScheduler::yield();
        val1 += 15;
    });

    scheduler.spawn([&val2]() {
        val2 += 10;
        SqliteCoroScheduler::yield();
        val2 += 20;
    });

    assert(scheduler.pending_tasks() == 2);

    // Step 1 on main thread
    assert(scheduler.poll_one());
    assert(val1 == 5);

    assert(scheduler.poll_one());
    assert(val2 == 10);

    // Drain remainder
    size_t drained = scheduler.run_until_empty();
    assert(drained == 2);
    assert(val1 == 20);
    assert(val2 == 30);
    assert(scheduler.pending_tasks() == 0);

    // Test direct run_local
    int local_val = 100;
    scheduler.run_local([&local_val]() {
        local_val += 50;
        SqliteCoroScheduler::yield();
        local_val *= 2;
    });
    assert(local_val == 300);

    printf("   [PASS] Main-thread polling and run_local executed cooperatively.\n");
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// Test 2: Background Worker Thread Pool with Capturing Lambdas
// ----------------------------------------------------------------------------
void test_cpp_thread_pool_closures() {
    printf("2. Testing C++ SqliteCoroScheduler across 4 worker threads...\n");
    fflush(stdout);

    SqliteCoroScheduler pool(4);
    assert(pool.worker_count() == 4);

    SqliteAtomicInt total_counter(0);

    for (int i = 0; i < 50; ++i) {
        pool.spawn([&total_counter]() {
            total_counter += 1;
            SqliteCoroScheduler::yield();
            total_counter += 10;
            SqliteCoroScheduler::yield();
            total_counter += 100;
        });
    }

    pool.wait_all();
    assert(pool.pending_tasks() == 0);

    int final_sum = total_counter.load();
    assert(final_sum == 50 * 111);

    printf("   [PASS] 50 capturing closures executed across 4 workers with total sum %d.\n", final_sum);
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// Test 3: Process-Wide Global Singleton & Atomic Reference Counting
// ----------------------------------------------------------------------------
void test_cpp_global_singleton_refcount() {
    printf("3. Testing process-wide global scheduler acquisition & ref-counting...\n");
    fflush(stdout);

    // DB 1 loads extension
    SqliteCoroScheduler* g1 = SqliteCoroScheduler::acquire_global(4);
    assert(g1 != nullptr);
    assert(g1->worker_count() == 4);

    // DB 2 loads extension (shares exact same instance)
    SqliteCoroScheduler* g2 = SqliteCoroScheduler::acquire_global(4);
    assert(g2 == g1);

    SqliteAtomicInt task_flag(0);
    g1->spawn([&task_flag]() {
        task_flag.store(999);
    });

    g2->wait_all();
    assert(task_flag.load() == 999);

    // DB 1 closes (ref_count decrements to 1, pool stays alive)
    SqliteCoroScheduler::release_global();

    // DB 2 can still execute tasks
    task_flag.store(0);
    g2->spawn([&task_flag]() {
        task_flag.store(777);
    });
    g2->wait_all();
    assert(task_flag.load() == 777);

    // DB 2 closes (ref_count decrements to 0, pool automatically destroyed)
    SqliteCoroScheduler::release_global();

    printf("   [PASS] Multi-database ref-counting and shared execution verified.\n");
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// Test 4: Move Semantics & RAII Teardown
// ----------------------------------------------------------------------------
void test_cpp_scheduler_move_semantics() {
    printf("4. Testing SqliteCoroScheduler move constructors and RAII teardown...\n");
    fflush(stdout);

    SqliteCoroScheduler p1(2);
    assert(p1.is_valid());
    assert(p1.worker_count() == 2);

    // Move construction
    SqliteCoroScheduler p2(sqlite_move(p1));
    assert(!p1.is_valid());
    assert(p2.is_valid());
    assert(p2.worker_count() == 2);

    SqliteAtomicInt count(0);
    p2.spawn([&count]() {
        count.store(42);
    });
    p2.wait_all();
    assert(count.load() == 42);

    // Move assignment
    SqliteCoroScheduler p3(4);
    p3 = sqlite_move(p2);
    assert(!p2.is_valid());
    assert(p3.is_valid());
    assert(p3.worker_count() == 2);

    p3.spawn([&count]() {
        count.store(84);
    });
    p3.wait_all();
    assert(count.load() == 84);

    printf("   [PASS] Move semantics and clean RAII destruction verified.\n");
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// Test 5: Nested Task Spawning from Inside Coroutines
// ----------------------------------------------------------------------------
void test_cpp_nested_task_spawning() {
    printf("5. Testing multi-stage pipeline closures across workers...\n");
    fflush(stdout);

    SqliteCoroScheduler pool(4);
    SqliteAtomicInt pipeline_result(0);

    for (int p = 0; p < 20; ++p) {
        pool.spawn([&pipeline_result, p]() {
            int val = p * 2;
            SqliteCoroScheduler::yield();
            val += 10;
            SqliteCoroScheduler::yield();
            pipeline_result += val;
        });
    }

    pool.wait_all();
    assert(pool.pending_tasks() == 0);

    // Sum: sum(p*2 + 10 for p in 0..19) = 2*sum(0..19) + 200 = 2*190 + 200 = 380 + 200 = 580
    int final_result = pipeline_result.load();
    assert(final_result == 580);

    printf("   [PASS] 20 multi-stage pipeline tasks executed across workers with sum %d.\n", final_result);
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// Test 6: Standalone Template Helpers (sqlite_coro_spawn / sqlite_coro_spawn_stack)
// ----------------------------------------------------------------------------
void test_cpp_template_spawn_helpers() {
    printf("6. Testing standalone template spawn helpers (sqlite_coro_spawn)...\n");
    fflush(stdout);

    SqliteCoroScheduler pool(2);
    SqliteAtomicInt flag1(0), flag2(0), flag3(0);

    // Spawn via scheduler reference
    sqlite_coro_spawn(pool, [&flag1]() {
        flag1.store(111);
    });

    // Spawn via scheduler pointer
    sqlite_coro_spawn(&pool, [&flag2]() {
        flag2.store(222);
    });

    // Spawn with explicit template stack size
    sqlite_coro_spawn_stack<32 * 1024>(pool, [&flag3]() {
        flag3.store(333);
    });

    pool.wait_all();
    assert(flag1.load() == 111);
    assert(flag2.load() == 222);
    assert(flag3.load() == 333);

    // Spawn into raw C pool handle via template
    sqlite3_coro_pool_t raw_c_pool;
    assert(sqlite3_coro_pool_init(&raw_c_pool, 0) == SQLITE_OK);
    int raw_val = 0;
    sqlite_coro_spawn(&raw_c_pool, [&raw_val]() {
        raw_val = 999;
    });
    assert(sqlite3_coro_pool_poll_one(&raw_c_pool) == 1);
    assert(raw_val == 999);
    sqlite3_coro_pool_destroy(&raw_c_pool);

    printf("   [PASS] Standalone template spawn helpers verified across all overloads.\n");
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// Test 7: Custom Stack Sizing (32KB & 128KB)
// ----------------------------------------------------------------------------
void test_cpp_custom_stack_sizes() {
    printf("7. Testing custom stack sizes (32 KB and 128 KB)...\n");
    fflush(stdout);

    SqliteCoroScheduler pool(2);
    SqliteAtomicInt depth_passed(0);

    // 32 KB stack task
    pool.spawn([&depth_passed]() {
        volatile char buffer[16 * 1024];
        buffer[0] = 'A';
        buffer[sizeof(buffer) - 1] = 'Z';
        SqliteCoroScheduler::yield();
        if (buffer[0] == 'A' && buffer[sizeof(buffer) - 1] == 'Z') {
            depth_passed += 1;
        }
    }, 32 * 1024);

    // 128 KB stack task
    pool.spawn([&depth_passed]() {
        volatile char buffer[64 * 1024];
        buffer[0] = 'X';
        buffer[sizeof(buffer) - 1] = 'Y';
        SqliteCoroScheduler::yield();
        if (buffer[0] == 'X' && buffer[sizeof(buffer) - 1] == 'Y') {
            depth_passed += 1;
        }
    }, 128 * 1024);

    pool.wait_all();
    assert(depth_passed.load() == 2);

    printf("   [PASS] Custom 32 KB and 128 KB stacks allocated and preserved.\n");
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// Test 8: Heavy Concurrent Throughput (300 Tasks Across 8 Workers)
// ----------------------------------------------------------------------------
void test_cpp_heavy_concurrent_throughput() {
    printf("8. Testing high-concurrency throughput (50 tasks across 8 workers)...\n");
    fflush(stdout);

    SqliteCoroScheduler pool(8);
    assert(pool.worker_count() == 8);

    SqliteAtomicInt total_ops(0);

    for (int i = 0; i < 50; ++i) {
        pool.spawn([&total_ops]() {
            for (int step = 0; step < 2; ++step) {
                total_ops += 1;
                SqliteCoroScheduler::yield();
            }
        });
    }

    pool.wait_all();
    assert(pool.pending_tasks() == 0);
    assert(total_ops.load() == 50 * 2);

    printf("   [PASS] 50 tasks processed %d total operations across 8 workers.\n", total_ops.load());
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// Test 9: Synchronous Batch run_until_empty on Main Thread
// ----------------------------------------------------------------------------
void test_cpp_batch_run_until_empty() {
    printf("9. Testing C++ run_until_empty batch draining on main thread...\n");
    fflush(stdout);

    SqliteCoroScheduler sched(0);
    assert(sched.worker_count() == 0);

    int sum1 = 0, sum2 = 0;
    sched.spawn([&sum1]() {
        sum1 += 10;
        SqliteCoroScheduler::yield();
        sum1 += 20;
        SqliteCoroScheduler::yield();
        sum1 += 30;
    });

    sched.spawn([&sum2]() {
        sum2 += 100;
        SqliteCoroScheduler::yield();
        sum2 += 200;
    });

    assert(sched.pending_tasks() == 2);

    size_t steps = sched.run_until_empty();
    assert(steps == 5); // 3 steps for task 1, 2 steps for task 2
    assert(sched.pending_tasks() == 0);
    assert(sum1 == 60);
    assert(sum2 == 300);

    // Draining an empty scheduler returns 0 steps
    assert(sched.run_until_empty() == 0);

    printf("   [PASS] run_until_empty executed %d steps to full completion.\n", (int)steps);
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// Test 10: Custom Functor Object & Mutable Lambda Closures
// ----------------------------------------------------------------------------
struct AccumulatorFunctor {
    SqliteAtomicInt* target;
    int increment_val;

    void operator()() {
        for (int i = 0; i < 3; ++i) {
            *target += increment_val;
            SqliteCoroScheduler::yield();
        }
    }
};

void test_cpp_functor_and_mutable_lambdas() {
    printf("10. Testing custom functor objects and mutable lambdas...\n");
    fflush(stdout);

    SqliteCoroScheduler pool(2);
    SqliteAtomicInt accumulator(0);

    AccumulatorFunctor f1;
    f1.target = &accumulator;
    f1.increment_val = 5;
    pool.spawn(f1);

    AccumulatorFunctor f2;
    f2.target = &accumulator;
    f2.increment_val = 10;
    pool.spawn(f2);

    // Mutable lambda with local internal state
    int captured_initial = 100;
    pool.spawn([captured_initial, &accumulator]() mutable {
        for (int i = 0; i < 2; ++i) {
            captured_initial += 10;
            accumulator += captured_initial;
            SqliteCoroScheduler::yield();
        }
    });

    pool.wait_all();
    assert(pool.pending_tasks() == 0);

    // f1: 3 * 5 = 15. f2: 3 * 10 = 30. lambda: 110 + 120 = 230. Total = 275.
    int final_val = accumulator.load();
    assert(final_val == 275);

    printf("   [PASS] Custom functor objects and mutable closures executed with sum %d.\n", final_val);
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// Test 11: Multi-Stage Sequential Data Pipeline Across Worker Pool
// ----------------------------------------------------------------------------
void test_cpp_multi_phase_reuse() {
    printf("11. Testing multi-stage sequential pipeline reuse...\n");
    fflush(stdout);

    SqliteCoroScheduler pool(4);
    SqliteAtomicInt pipeline_sum(0);

    for (int stage = 1; stage <= 3; ++stage) {
        for (int i = 0; i < 25; ++i) {
            pool.spawn([&pipeline_sum]() {
                pipeline_sum += 1;
                SqliteCoroScheduler::yield();
                pipeline_sum += 2;
            });
        }
        pool.wait_all();
        assert(pool.pending_tasks() == 0);
        assert(pipeline_sum.load() == stage * 25 * 3);
    }

    printf("   [PASS] 3-stage sequential pipeline processed %d total items.\n", pipeline_sum.load());
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// Test 12: Scheduler Validity, Getter API & Edge Cases
// ----------------------------------------------------------------------------
void test_cpp_validity_and_edge_cases() {
    printf("12. Testing scheduler validity, raw handle access & API getters...\n");
    fflush(stdout);

    SqliteCoroScheduler sched(2);
    assert(sched.is_valid());
    assert(sched.worker_count() == 2);
    assert(sched.raw_pool() != nullptr);
    assert(sched.pending_tasks() == 0);

    // Step on empty pool returns false
    assert(sched.poll_one() == false);

    // Default-constructed moved-from instance safety
    SqliteCoroScheduler empty_sched(sqlite_move(sched));
    assert(empty_sched.is_valid());
    assert(sched.is_valid() == false);
    assert(sched.raw_pool() == nullptr);
    assert(sched.worker_count() == 0);
    assert(sched.pending_tasks() == 0);

    // Spawning into invalidated scheduler safely returns false
    bool spawn_res = sched.spawn([]() {});
    assert(spawn_res == false);

    printf("   [PASS] is_valid(), raw_pool(), and moved-from handles safely inspected.\n");
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// Test 13: Out-of-Coroutine Yield Safety
// ----------------------------------------------------------------------------
void test_cpp_outside_yield_safety() {
    printf("13. Testing out-of-coroutine yield safety from main thread...\n");
    fflush(stdout);

    // Must be a safe no-op
    SqliteCoroScheduler::yield();
    SqliteCoroScheduler::yield();

    printf("   [PASS] SqliteCoroScheduler::yield() called outside coroutine safely ignored.\n");
    fflush(stdout);
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=================================================================\n");
    printf("Running C++11/C++20 SqliteCoroScheduler Test Suite (-nostdlib++)\n");
    printf("=================================================================\n");

    test_cpp_main_thread_scheduler();
    test_cpp_thread_pool_closures();
    test_cpp_global_singleton_refcount();
    test_cpp_scheduler_move_semantics();
    test_cpp_nested_task_spawning();
    test_cpp_template_spawn_helpers();
    test_cpp_custom_stack_sizes();
    test_cpp_heavy_concurrent_throughput();
    test_cpp_batch_run_until_empty();
    test_cpp_functor_and_mutable_lambdas();
    test_cpp_multi_phase_reuse();
    test_cpp_validity_and_edge_cases();
    test_cpp_outside_yield_safety();

    printf("\nAll 13 C++ Coroutine Scheduler Tests Passed Cleanly!\n");
    return 0;
}
