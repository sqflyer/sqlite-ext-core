#include <stdio.h>
#include <assert.h>
#include "sqlite3_time.h"

void test_monotonic_clock(void) {
    printf("1. Testing Pure C monotonic clock precision...\n");
    uint64_t start_ns = sqlite3_time_ns();
    uint64_t start_us = sqlite3_time_us();
    uint64_t start_ms = sqlite3_time_ms();

    sqlite3_time_sleep_ms(20);

    uint64_t end_ns = sqlite3_time_ns();
    uint64_t end_us = sqlite3_time_us();
    uint64_t end_ms = sqlite3_time_ms();

    assert(end_ns > start_ns);
    assert(end_us >= start_us + 15); // At least 15ms elapsed
    assert(end_ms >= start_ms + 15);
    printf("   [PASS] Monotonic elapsed: %llu ms (%llu us)\n", (unsigned long long)(end_ms - start_ms), (unsigned long long)(end_us - start_us));
}

void test_wall_clock(void) {
    printf("2. Testing Pure C wall-clock epoch timestamps...\n");
    int64_t sec = sqlite3_time_now_sec();
    int64_t ms = sqlite3_time_now_ms();

    assert(sec > 1700000000LL); // Reasonable unix timestamp (> late 2023)
    assert(ms >= (sec - 1) * 1000LL);
    assert(ms <= (sec + 2) * 1000LL);
    printf("   [PASS] Epoch sec: %lld, Epoch ms: %lld\n", (long long)sec, (long long)ms);
}

void test_timezone_offset(void) {
    printf("3. Testing Pure C timezone offset detection...\n");
    long offset = sqlite3_time_timezone_offset_sec();
    // Offset must be within -12h (-43200s) to +14h (+50400s)
    assert(offset >= -43200L && offset <= 50400L);
    printf("   [PASS] System timezone offset: %ld seconds (%ld hours, %ld mins)\n", 
           offset, offset / 3600L, (offset % 3600L) / 60L);
}

int main(void) {
    printf("=================================================================\n");
    printf("Running Pure C Time & Clock Test Suite\n");
    printf("=================================================================\n");

    test_monotonic_clock();
    test_wall_clock();
    test_timezone_offset();

    printf("\nAll Pure C Time Tests Passed Cleanly!\n");
    return 0;
}
