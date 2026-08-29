/**
 * @file test_thread_c.c
 * @brief Pure C Thread Lifecycle and Primitives Test Suite.
 *
 * Verifies cross-platform Pure C thread creation, worker execution, return value capture on join,
 * detached thread execution, and cooperative CPU timeslice yielding.
 */

#include <stdio.h>
#include <assert.h>
#include "sqlite3_thread.h"

/**
 * @brief Worker function modifying data payload and returning an integer exit code.
 * @param arg Pointer to integer payload.
 * @return Exit status pointer (1234).
 */
static void* worker_increment(void* arg) {
    int* val = (int*)arg;
    sqlite3_time_sleep_ms(20);
    *val += 100;
    return (void*)1234;
}

/**
 * @brief Worker function executing in detached mode.
 * @param arg Pointer to integer payload.
 * @return NULL.
 */
static void* worker_detach(void* arg) {
    int* val = (int*)arg;
    sqlite3_time_sleep_ms(10);
    *val = 999;
    return NULL;
}

/**
 * @brief Test 1: Thread creation, argument passing, join synchronization, and return value capture.
 */
void test_thread_create_and_join(void) {
    printf("1. Testing Pure C thread creation and join with return value...\n");
    sqlite3_thread_t th;
#if defined(_WIN32) || defined(_WIN64)
    th.handle = NULL;
    th.func = NULL;
    th.arg = NULL;
    th.retval = NULL;
    th.is_detached = 0;
#else
    th = (pthread_t)0;
#endif
    int data = 42;

    int rc = sqlite3_thread_create(&th, worker_increment, &data);
    assert(rc == 0);

    void* retval = NULL;
    rc = sqlite3_thread_join(&th, &retval);
    assert(rc == 0);
    assert(data == 142);
    assert(retval == (void*)1234);
    printf("   [PASS] Worker thread modified data to %d and returned %p\n", data, retval);
}

/**
 * @brief Test 2: Thread detachment and asynchronous execution without parent joining.
 */
void test_thread_detach(void) {
    printf("2. Testing Pure C thread detachment...\n");
    sqlite3_thread_t th;
#if defined(_WIN32) || defined(_WIN64)
    th.handle = NULL;
    th.func = NULL;
    th.arg = NULL;
    th.retval = NULL;
    th.is_detached = 0;
#else
    th = (pthread_t)0;
#endif
    int data = 0;

    int rc = sqlite3_thread_create(&th, worker_detach, &data);
    assert(rc == 0);

    rc = sqlite3_thread_detach(&th);
    assert(rc == 0);

    // Wait for detached thread to execute
    sqlite3_time_sleep_ms(40);
    assert(data == 999);
    printf("   [PASS] Detached thread successfully executed asynchronously.\n");
}

/**
 * @brief Test 3: Relinquishing execution timeslice via sqlite3_thread_yield.
 */
void test_thread_yield(void) {
    printf("3. Testing Pure C thread yield...\n");
    sqlite3_thread_yield();
    printf("   [PASS] sqlite3_thread_yield executed cleanly.\n");
}

int main(void) {
    printf("=================================================================\n");
    printf("Running Pure C Thread Lifecycle Test Suite\n");
    printf("=================================================================\n");

    test_thread_create_and_join();
    test_thread_detach();
    test_thread_yield();

    printf("\nAll Pure C Thread Lifecycle Tests Passed Cleanly!\n");
    return 0;
}
