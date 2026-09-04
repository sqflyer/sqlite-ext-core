#include <stdio.h>
#include <assert.h>
#include "sqlite3_time.hpp"

void test_sqlite_clock() {
    printf("1. Testing SqliteClock C++ static methods...\n");
    uint64_t ns = SqliteClock::monotonic_ns();
    uint64_t us = SqliteClock::monotonic_us();
    uint64_t ms = SqliteClock::monotonic_ms();
    int64_t sec = SqliteClock::now_sec();
    int64_t now_ms = SqliteClock::now_ms();

    assert(ns > 0);
    assert(us > 0);
    assert(ms > 0);
    assert(sec > 1700000000LL);
    assert(now_ms > 0);

    // Verify monotonic non-decreasing across rapid calls
    uint64_t prev_ns = ns;
    for (int i = 0; i < 100; ++i) {
        uint64_t curr_ns = SqliteClock::monotonic_ns();
        assert(curr_ns >= prev_ns);
        prev_ns = curr_ns;
    }

    // Microsecond sleep
    SqliteClock::sleep_for_us(5000);
    uint64_t us2 = SqliteClock::monotonic_us();
    assert(us2 >= us + 3000);

    // Millisecond sleep
    SqliteClock::sleep_for_ms(15);
    uint64_t ms2 = SqliteClock::monotonic_ms();
    assert(ms2 >= ms + 10);
    printf("   [PASS] SqliteClock verified successfully.\n");
}

void test_sqlite_stopwatch() {
    printf("2. Testing SqliteStopwatch RAII benchmarking...\n");
    SqliteStopwatch sw;

    SqliteClock::sleep_for_ms(25);

    uint64_t elapsed_ms = sw.elapsed_ms();
    uint64_t elapsed_us = sw.elapsed_us();
    uint64_t elapsed_ns = sw.elapsed_ns();
    double elapsed_sec = sw.elapsed_sec();

    assert(elapsed_ms >= 20);
    assert(elapsed_us >= 20000);
    assert(elapsed_ns >= 20000000);
    assert(elapsed_sec >= 0.020);

    sw.restart();
    SqliteClock::sleep_for_ms(10);
    assert(sw.elapsed_ms() >= 8);
    printf("   [PASS] SqliteStopwatch measured: %llu ms (%.3f s)\n", 
           static_cast<unsigned long long>(elapsed_ms), elapsed_sec);
}

void test_sqlite_timezone() {
    printf("3. Testing SqliteTimezone helpers...\n");
    long sec = SqliteTimezone::offset_seconds();
    int hours = SqliteTimezone::offset_hours();
    int mins = SqliteTimezone::offset_minutes();

    assert(sec >= -43200L && sec <= 50400L);
    assert(mins >= 0 && mins < 60);

    // Verify exact decomposition
    long reconstructed = (long)hours * 3600L + (long)(sec >= 0 ? mins : -mins) * 60L;
    assert(reconstructed == sec);

    printf("   [PASS] System Timezone: %+d:%02d (%ld seconds)\n", hours, mins, sec);
}

int main() {
    printf("=================================================================\n");
    printf("Running C++11 Time & Clock Test Suite (-nostdlib++)\n");
    printf("=================================================================\n");

    test_sqlite_clock();
    test_sqlite_stopwatch();
    test_sqlite_timezone();

    printf("\nAll C++11 Time Tests Passed Cleanly!\n");
    return 0;
}
