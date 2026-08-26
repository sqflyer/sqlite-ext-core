#include <stdio.h>
#include <assert.h>
#include "sqlite3_thread.hpp"

static int g_free_fn_val = 0;
static void free_fn() {
    SqliteThread::sleep_for_ms(20);
    g_free_fn_val = 555;
}

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

int main() {
    printf("=================================================================\n");
    printf("Running C++11 SqliteThread Test Suite (-nostdlib++)\n");
    printf("=================================================================\n");

    test_thread_function_ptr();
    test_thread_lambda_capture();
    test_thread_move_semantics();
    test_thread_detach();

    printf("\nAll C++11 SqliteThread Tests Passed Cleanly!\n");
    return 0;
}
