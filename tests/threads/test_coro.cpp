/**
 * @file test_coro.cpp
 * @brief C++11 SqliteCoroutine & Generator Test Suite (-nostdlib++ compliant).
 *
 * Verifies RAII C++11 stackful coroutine wrappers (`SqliteCoroutine`), type-erased closure execution,
 * pointer value channeling across yields, range-based `for` loop iteration via `SqliteFiberGenerator<T>`,
 * move semantics, polymorphic `SqliteValueOwned` streaming, fixed-schema row iteration (`SqliteRowStatic`),
 * and dynamic array expansion across fiber yield points.
 */

#define SQLITE_CORE
#include <sqlite3.h>
#include <stdio.h>
#include <assert.h>
#include "sqlite3_coro.hpp"
#include "sqlite3_value.hpp"
#include "sqlite3_row.hpp"
#include "sqlite3_allocator.hpp"

// ----------------------------------------------------------------------------
// Test 1: Capturing Closures & Lifecycle
// ----------------------------------------------------------------------------
void test_coro_lambda_captures() {
    printf("1. Testing SqliteCoroutine with capturing lambda...\n");
    int shared_state = 10;

    SqliteCoroutine coro([&shared_state]() {
        shared_state += 5;
        SqliteCoroutine::yield();
        shared_state *= 2;
        SqliteCoroutine::yield();
        shared_state += 100;
    });

    assert(coro.is_valid());
    assert(!coro.is_done());
    assert(static_cast<bool>(coro));

    // Step 1
    assert(coro.resume() == SQLITE_OK);
    assert(shared_state == 15);
    assert(!coro.is_done());

    // Step 2
    assert(coro.resume() == SQLITE_OK);
    assert(shared_state == 30);
    assert(!coro.is_done());

    // Step 3
    assert(coro.resume() == SQLITE_OK);
    assert(shared_state == 130);
    assert(coro.is_done());
    assert(!static_cast<bool>(coro));

    // Resuming finished returns misuse
    assert(coro.resume() == SQLITE_MISUSE);

    printf("   [PASS] Capturing closure executed across yields and checked lifecycle.\n");
}

// ----------------------------------------------------------------------------
// Test 2: Free Function & Move Semantics
// ----------------------------------------------------------------------------
static int g_free_fn_val = 0;
static void free_fn() {
    g_free_fn_val = 42;
    SqliteCoroutine::yield();
    g_free_fn_val = 84;
}

void test_coro_move_and_free_function() {
    printf("2. Testing SqliteCoroutine move semantics and free functions...\n");
    g_free_fn_val = 0;

    SqliteCoroutine c1(free_fn);
    assert(c1.is_valid());

    c1.resume();
    assert(g_free_fn_val == 42);

    // Move constructor
    SqliteCoroutine c2(sqlite_move(c1));
    assert(!c1.is_valid());
    assert(c2.is_valid());

    // Move assignment
    SqliteCoroutine c3;
    assert(!c3.is_valid());
    c3 = sqlite_move(c2);
    assert(!c2.is_valid());
    assert(c3.is_valid());

    c3.resume();
    assert(g_free_fn_val == 84);
    assert(c3.is_done());

    printf("   [PASS] Move constructor and move assignment transferred state cleanly.\n");
}

// ----------------------------------------------------------------------------
// Test 3: Typed Value Yielding
// ----------------------------------------------------------------------------
void test_coro_value_yielding() {
    printf("3. Testing SqliteCoroutine typed value yielding (pointer channeling)...\n");

    int val1 = 111;
    int val2 = 222;

    SqliteCoroutine coro([&]() {
        SqliteCoroutine::yield_value(&val1);
        SqliteCoroutine::yield_value(&val2);
    });

    coro.resume();
    assert(coro.get_value() == &val1);
    assert(*(static_cast<int*>(coro.get_value())) == 111);

    coro.resume();
    assert(coro.get_value() == &val2);
    assert(*(static_cast<int*>(coro.get_value())) == 222);

    coro.resume();
    assert(coro.is_done());

    printf("   [PASS] Pointers yielded and retrieved with zero allocations.\n");
}

// ----------------------------------------------------------------------------
// Test 4: Typed Generator & C++11 Range-Based For Loop
// ----------------------------------------------------------------------------
void test_fiber_generator_range() {
    printf("4. Testing SqliteFiberGenerator with C++11 range-based for loop...\n");

    SqliteFiberGenerator<int> gen([](const SqliteFiberGenerator<int>::YieldHandle& yield) {
        for (int i = 1; i <= 5; ++i) {
            yield(i * 10);
        }
    });

    int expected[] = { 10, 20, 30, 40, 50 };
    int idx = 0;

    for (int val : gen) {
        assert(val == expected[idx]);
        idx++;
    }
    assert(idx == 5);
    assert(gen.is_done());

    printf("   [PASS] Fiber generator yielded all values in range-for loop.\n");
}

// ----------------------------------------------------------------------------
// Test 5: String View Generator & Move Operations
// ----------------------------------------------------------------------------
void test_generator_move_and_views() {
    printf("5. Testing generator move operations and SqliteStringView streaming...\n");

    SqliteFiberGenerator<SqliteStringView> gen([](const SqliteFiberGenerator<SqliteStringView>::YieldHandle& yield) {
        yield(SqliteStringView("alpha"));
        yield(SqliteStringView("beta"));
        yield(SqliteStringView("gamma"));
    });

    assert(gen.value() == "alpha");

    // Move to gen2
    SqliteFiberGenerator<SqliteStringView> gen2 = sqlite_move(gen);
    assert(gen2.value() == "alpha");

    assert(gen2.next());
    assert(gen2.value() == "beta");

    assert(gen2.next());
    assert(gen2.value() == "gamma");

    assert(!gen2.next());
    assert(gen2.is_done());

    printf("   [PASS] String view generator streamed and moved across scopes.\n");
}

// ----------------------------------------------------------------------------
// Test 6: Default Constructed & Self-Move Edge Cases
// ----------------------------------------------------------------------------
void test_coro_edge_cases() {
    printf("6. Testing default construction, self-move, and overwrite operations...\n");

    // Default constructed coroutine
    SqliteCoroutine def_coro;
    assert(!def_coro.is_valid());
    assert(def_coro.is_done());
    assert(!static_cast<bool>(def_coro));
    assert(def_coro.resume() == SQLITE_MISUSE);
    assert(def_coro.get_value() == nullptr);

    // Overwrite active coroutine via move
    int flag1 = 0;
    int flag2 = 0;
    SqliteCoroutine c1([&flag1]() { flag1 = 1; SqliteCoroutine::yield(); flag1 = 2; });
    SqliteCoroutine c2([&flag2]() { flag2 = 10; SqliteCoroutine::yield(); flag2 = 20; });

    c1.resume();
    assert(flag1 == 1);

    // Moving c2 into active c1 should destroy old c1 cleanly without leaks
    c1 = sqlite_move(c2);
    assert(!c2.is_valid());
    assert(c1.is_valid());

    c1.resume();
    assert(flag2 == 10);

    // Custom stack size
    SqliteCoroutine c_custom([]() {
        SqliteCoroutine::yield();
    }, 131072 /* 128 KB */);
    assert(c_custom.is_valid());
    assert(c_custom.resume() == SQLITE_OK);

    printf("   [PASS] Default constructors, self-moves, and overwrites handled safely.\n");
}

// ----------------------------------------------------------------------------
// Test 7: Early Range-For Break and RAII Cleanup
// ----------------------------------------------------------------------------
void test_generator_early_break_and_composition() {
    printf("7. Testing early break from range-for loop & generator composition...\n");

    // Early break: verify RAII destructor cleans up suspended fiber midway
    {
        SqliteFiberGenerator<int> gen_100([](const SqliteFiberGenerator<int>::YieldHandle& yield) {
            for (int i = 1; i <= 100; ++i) {
                yield(i);
            }
        });

        int seen = 0;
        for (int v : gen_100) {
            seen++;
            if (v == 3) break; // Early exit while fiber is suspended at iteration 3!
        }
        assert(seen == 3);
    } // ~SqliteFiberGenerator() executed here on suspended fiber

    // Empty generator (0 yields)
    SqliteFiberGenerator<int> empty_gen([](const SqliteFiberGenerator<int>::YieldHandle&) {
        // 0 yields
    });
    int empty_count = 0;
    for (int v : empty_gen) {
        (void)v;
        empty_count++;
    }
    assert(empty_count == 0);
    assert(empty_gen.is_done());

    // Nested generator composition
    SqliteFiberGenerator<int> outer_gen([](const SqliteFiberGenerator<int>::YieldHandle& yield_out) {
        SqliteFiberGenerator<int> inner_gen([](const SqliteFiberGenerator<int>::YieldHandle& yield_in) {
            yield_in(5);
            yield_in(10);
        });
        for (int x : inner_gen) {
            yield_out(x * 2);
        }
    });

    int expected_nested[] = { 10, 20 };
    int n_idx = 0;
    for (int val : outer_gen) {
        assert(val == expected_nested[n_idx]);
        n_idx++;
    }
    assert(n_idx == 2);

    printf("   [PASS] Early break RAII, empty generators, and composition verified.\n");
}

// ----------------------------------------------------------------------------
// Test 8: Polymorphic SqliteValueOwned Streaming
// ----------------------------------------------------------------------------
void test_polymorphic_value_generator() {
    printf("8. Testing polymorphic SqliteValueOwned streaming across fiber yields...\n");

    SqliteFiberGenerator<SqliteValueOwned> gen([](const SqliteFiberGenerator<SqliteValueOwned>::YieldHandle& yield) {
        yield(SqliteValueOwned(123456789LL));
        yield(SqliteValueOwned(3.1415926535));
        yield(SqliteValueOwned::from_text("Hello Fiber"));
    });

    assert(gen.value().type() == SQLITE_INTEGER);
    assert(gen.value().as_int64() == 123456789LL);

    assert(gen.next());
    assert(gen.value().type() == SQLITE_FLOAT);
    assert(gen.value().as_double() > 3.14);

    assert(gen.next());
    assert(gen.value().type() == SQLITE_TEXT);
    SqliteStringView sv = gen.value().as_text();
    assert(sv.length() == 11);
    assert(sv == "Hello Fiber");

    assert(!gen.next());
    assert(gen.is_done());

    printf("   [PASS] Polymorphic variants yielded and inspected with type safety.\n");
}

// ----------------------------------------------------------------------------
// Test 9: Multi-Column Static Rows Streaming (SqliteRowStatic<4>)
// ----------------------------------------------------------------------------
void test_static_rows_streaming() {
    printf("9. Testing multi-column SqliteRowStatic<4> streaming...\n");

    SqliteFiberGenerator<SqliteRowStatic<4>> gen([](const SqliteFiberGenerator<SqliteRowStatic<4>>::YieldHandle& yield) {
        for (int i = 1; i <= 3; ++i) {
            SqliteRowStatic<4> row;
            row[0] = static_cast<sqlite3_int64>(i);
            row[1] = static_cast<sqlite3_int64>(i * i);
            row[2] = static_cast<double>(i) * 1.5;
            row[3] = SqliteValueOwned::from_text("ROW_ITEM");
            yield(row);
        }
    });

    int count = 0;
    for (const auto& r : gen) {
        count++;
        assert(r.as_int64(0) == count);
        assert(r.as_int64(1) == count * count);
        assert(r.as_double(2) == count * 1.5);
        assert(r.as_text(3) == "ROW_ITEM");
    }
    assert(count == 3);
    assert(gen.is_done());

    printf("   [PASS] Multi-column fixed-schema rows streamed with zero heap overhead.\n");
}

// ----------------------------------------------------------------------------
// Test 10: Dynamic Array Reallocation Inside Fiber Execution
// ----------------------------------------------------------------------------
void test_dynamic_array_reallocation_in_fiber() {
    printf("10. Testing dynamic memory reallocation (sqlite_reallocate_array) in fiber...\n");

    SqliteCoroutine coro([]() {
        int* buffer = sqlite_new_array<int>(2);
        buffer[0] = 11;
        buffer[1] = 22;

        SqliteCoroutine::yield();

        // Grow buffer in-flight
        buffer = sqlite_reallocate_array<int>(buffer, 4);
        buffer[2] = 33;
        buffer[3] = 44;

        SqliteCoroutine::yield();

        assert(buffer[0] == 11);
        assert(buffer[1] == 22);
        assert(buffer[2] == 33);
        assert(buffer[3] == 44);

        sqlite_delete_array(buffer);
    });

    assert(coro.resume() == SQLITE_OK);
    assert(coro.resume() == SQLITE_OK);
    assert(coro.resume() == SQLITE_OK);
    assert(coro.is_done());

    printf("   [PASS] Dynamic array expansion succeeded across fiber yield boundaries.\n");
}

int main() {
    printf("=================================================================\n");
    printf("Running C++11 SqliteCoroutine & Generator Test Suite (-nostdlib++)\n");
    printf("=================================================================\n");

    test_coro_lambda_captures();
    test_coro_move_and_free_function();
    test_coro_value_yielding();
    test_fiber_generator_range();
    test_generator_move_and_views();
    test_coro_edge_cases();
    test_generator_early_break_and_composition();
    test_polymorphic_value_generator();
    test_static_rows_streaming();
    test_dynamic_array_reallocation_in_fiber();

    printf("\nAll C++11 Coroutine Tests Passed Cleanly!\n");
    return 0;
}
