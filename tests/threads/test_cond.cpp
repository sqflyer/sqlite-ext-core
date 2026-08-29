/**
 * @file test_cond.cpp
 * @brief C++11 SqliteConditionVariable & Synchronization Test Suite (-nostdlib++ compliant).
 *
 * Verifies predicate-based waiting loops guarding against spurious wakeups, accurate millisecond
 * timeout detection (`wait_for`), and multi-worker broadcast signaling (`notify_all`).
 */

#include <stdio.h>
#include <assert.h>
#include "sqlite3_thread.hpp"

/**
 * @brief Test 1: Spurious-wakeup-safe predicate waiting on SqliteConditionVariable.
 */
void test_condition_variable_predicate() {
    printf("1. Testing SqliteConditionVariable wait with predicate...\n");
    SqliteThreadMutex mutex;
    SqliteConditionVariable cond;
    bool ready = false;
    int payload = 0;

    SqliteThread worker([&]() {
        SqliteThread::sleep_for_ms(25);
        {
            SqliteThreadMutexGuard lock(mutex);
            payload = 4242;
            ready = true;
        }
        cond.notify_one();
    });

    {
        SqliteThreadMutexGuard lock(mutex);
        cond.wait(lock, [&]() { return ready; });
        assert(payload == 4242);
    }

    worker.join();
    printf("   [PASS] Predicate wait completed successfully.\n");
}

/**
 * @brief Test 2: Timed waiting via wait_for with accurate timeout detection.
 */
void test_condition_variable_wait_for() {
    printf("2. Testing SqliteConditionVariable wait_for with timeout...\n");
    SqliteThreadMutex mutex;
    SqliteConditionVariable cond;
    bool triggered = false;

    SqliteThreadMutexGuard lock(mutex);
    uint64_t start_ms = sqlite3_time_ms();
    bool signaled = cond.wait_for(lock, 30, [&]() { return triggered; });
    uint64_t elapsed_ms = sqlite3_time_ms() - start_ms;

    assert(!signaled);
    assert(!triggered);
    assert(elapsed_ms >= 20);
    printf("   [PASS] wait_for timed out accurately after %llu ms.\n", 
           static_cast<unsigned long long>(elapsed_ms));
}

/**
 * @brief Test 3: Waking multiple concurrent workers via notify_all.
 */
void test_condition_variable_broadcast() {
    printf("3. Testing SqliteConditionVariable notify_all to multiple workers...\n");
    SqliteThreadMutex mutex;
    SqliteConditionVariable cond;
    bool start_signal = false;
    int completed_workers = 0;

    const int NUM_THREADS = 4;
    SqliteThread threads[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads[i] = SqliteThread([&]() {
            SqliteThreadMutexGuard lock(mutex);
            cond.wait(lock, [&]() { return start_signal; });
            completed_workers++;
        });
    }

    SqliteThread::sleep_for_ms(20);

    {
        SqliteThreadMutexGuard lock(mutex);
        start_signal = true;
    }
    cond.notify_all();

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads[i].join();
    }

    assert(completed_workers == NUM_THREADS);
    printf("   [PASS] notify_all woke up all %d workers.\n", NUM_THREADS);
}

int main() {
    printf("=================================================================\n");
    printf("Running C++11 SqliteConditionVariable Test Suite (-nostdlib++)\n");
    printf("=================================================================\n");

    test_condition_variable_predicate();
    test_condition_variable_wait_for();
    test_condition_variable_broadcast();

    printf("\nAll C++11 Condition Variable Tests Passed Cleanly!\n");
    return 0;
}
