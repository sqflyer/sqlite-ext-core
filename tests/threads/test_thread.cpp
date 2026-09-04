/**
 * @file test_thread.cpp
 * @brief C++11 SqliteThread Wrapper Test Suite (-nostdlib++ compliant).
 *
 * Verifies RAII thread management, parameterless function pointer dispatch, capturing lambda execution,
 * move construction, move assignment, and detached thread behavior without standard library dependencies.
 */

#include <stdio.h>
#include <assert.h>
#include "sqlite3_thread.hpp"

static int g_free_fn_val = 0;

/**
 * @brief Free function worker executed by SqliteThread.
 */
static void free_fn() {
    SqliteThread::sleep_for_ms(20);
    g_free_fn_val = 555;
}

/**
 * @brief Test 1: Spawning a thread using a raw C++ function pointer.
 */
void test_thread_function_ptr() {
    printf("1. Testing SqliteThread with function pointer...\n");
    g_free_fn_val = 0;
    {
        SqliteThread th(free_fn);
        assert(th.joinable());
        th.join();
        assert(!th.joinable());
    }
    assert(g_free_fn_val == 555);
    printf("   [PASS] Function pointer thread executed.\n");
}

/**
 * @brief Test 2: Spawning a thread using a stateful capturing lambda closure.
 */
void test_thread_lambda_capture() {
    printf("2. Testing SqliteThread with capturing lambda...\n");
    int val = 0;
    {
        SqliteThread th([&val]() {
            SqliteThread::sleep_for_ms(20);
            val = 999;
        });
        assert(th.joinable());
        th.join();
        assert(!th.joinable());
    }
    assert(val == 999);
    printf("   [PASS] Lambda thread executed and mutated captured variable.\n");
}

/**
 * @brief Test 3: Verifying move constructor and move assignment semantics for SqliteThread.
 */
void test_thread_move_semantics() {
    printf("3. Testing SqliteThread move semantics...\n");
    int state = 0;
    SqliteThread th1([&state]() {
        SqliteThread::sleep_for_ms(10);
        state = 123;
    });

    assert(th1.joinable());

    // Move constructor
    SqliteThread th2(sqlite_move(th1));
    assert(!th1.joinable());
    assert(th2.joinable());

    // Move assignment
    SqliteThread th3;
    th3 = sqlite_move(th2);
    assert(!th2.joinable());
    assert(th3.joinable());

    th3.join();
    assert(state == 123);
    printf("   [PASS] Thread moved cleanly across instances.\n");
}

/**
 * @brief Test 4: Detaching a thread to run independently in the background.
 */
void test_thread_detach() {
    printf("4. Testing SqliteThread detach()...\n");
    int counter = 0;
    {
        SqliteThread th([&counter]() {
            SqliteThread::sleep_for_ms(10);
            counter = 9999;
        });
        assert(th.joinable());
        th.detach();
        assert(!th.joinable());
    }

    SqliteThread::sleep_for_ms(30);
    assert(counter == 9999);
    printf("   [PASS] Detached thread ran in background.\n");
}

static SQLITE_THREAD_LOCAL int g_thread_local_val = 0;
static SQLITE_THREAD_LOCAL void* g_thread_local_addr = nullptr;

/**
 * @brief Test 5: Verifying SQLITE_THREAD_LOCAL thread-local storage isolation across threads.
 */
void test_thread_local_storage() {
    printf("5. Testing SQLITE_THREAD_LOCAL isolation across threads...\n");

    // Initialize in main thread
    g_thread_local_val = 777;
    g_thread_local_addr = &g_thread_local_val;

    int worker1_read = 0;
    int worker2_read = 0;
    void* worker1_addr = nullptr;
    void* worker2_addr = nullptr;

    SqliteThread th1([&worker1_read, &worker1_addr]() {
        // Must start with 0 in the new thread
        assert(g_thread_local_val == 0);
        g_thread_local_val = 111;
        worker1_addr = (void*)&g_thread_local_val;
        SqliteThread::sleep_for_ms(20);
        worker1_read = g_thread_local_val;
    });

    SqliteThread th2([&worker2_read, &worker2_addr]() {
        assert(g_thread_local_val == 0);
        g_thread_local_val = 222;
        worker2_addr = (void*)&g_thread_local_val;
        SqliteThread::sleep_for_ms(20);
        worker2_read = g_thread_local_val;
    });

    th1.join();
    th2.join();

    // Main thread's TLS value was undisturbed
    assert(g_thread_local_val == 777);
    assert(g_thread_local_addr == (void*)&g_thread_local_val);

    // Both workers read their respective TLS values
    assert(worker1_read == 111);
    assert(worker2_read == 222);

    // Each thread had a distinct memory address for the TLS variable
    assert(worker1_addr != nullptr);
    assert(worker2_addr != nullptr);
    assert(worker1_addr != g_thread_local_addr);
    assert(worker2_addr != g_thread_local_addr);
    assert(worker1_addr != worker2_addr);

    printf("   [PASS] SQLITE_THREAD_LOCAL verified independent storage across threads.\n");
}

int main() {
    printf("=================================================================\n");
    printf("Running C++11 SqliteThread Test Suite (-nostdlib++)\n");
    printf("=================================================================\n");

    test_thread_function_ptr();
    test_thread_lambda_capture();
    test_thread_move_semantics();
    test_thread_detach();
    test_thread_local_storage();

    printf("\nAll C++11 SqliteThread Tests Passed Cleanly!\n");
    return 0;
}
