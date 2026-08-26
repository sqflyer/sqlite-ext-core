# Freestanding Time & Clock Subsystem (`sqlite3_time.h` / `sqlite3_time.hpp`)

This subsystem provides zero-dependency, high-resolution monotonic timers, wall-clock epoch timestamps, millisecond/microsecond sleeping routines, and automatic system timezone offset detection for both Pure C (C99/C11) and modern C++11.

It is strictly compliant with `-nostdlib` and `-nostdlib++` (zero dependency on `<chrono>`).

---

## 1. C++11 Quickstart (`sqlite3_time.hpp`)

### High-Precision Monotonic & Wall Time
```cpp
#include "sqlite3_time.hpp"

// 1. Monotonic high-resolution timestamps
uint64_t ns = SqliteClock::monotonic_ns();
uint64_t us = SqliteClock::monotonic_us();
uint64_t ms = SqliteClock::monotonic_ms();

// 2. Wall-clock Unix epoch
int64_t epoch_sec = SqliteClock::now_sec();
int64_t epoch_ms  = SqliteClock::now_ms();

// 3. Sleep routines
SqliteClock::sleep_for_ms(100);
SqliteClock::sleep_for_us(500);
```

### RAII Benchmarking Stopwatch (`SqliteStopwatch`)
```cpp
#include "sqlite3_time.hpp"

void benchmark_query(SqliteDatabaseView db) {
    SqliteStopwatch sw;

    db.exec("SELECT * FROM large_table;");

    printf("Query finished in %llu ms (%.4f seconds)\n", 
           (unsigned long long)sw.elapsed_ms(), 
           sw.elapsed_sec());

    sw.restart(); // Reset to measure next phase
}
```

### System Timezone Introspection (`SqliteTimezone`)
```cpp
#include "sqlite3_time.hpp"

long sec_offset = SqliteTimezone::offset_seconds(); // e.g. +19800
int hours = SqliteTimezone::offset_hours();          // +5
int mins  = SqliteTimezone::offset_minutes();        // 30
printf("Local Timezone: %+d:%02d\n", hours, mins);
```

---

## 2. Pure C Quickstart (`sqlite3_time.h`)

```c
#include "sqlite3_time.h"

void measure_c_operation(void) {
    uint64_t start_ms = sqlite3_time_ms();

    sqlite3_time_sleep_ms(50); // Cross-platform sleep

    uint64_t elapsed_ms = sqlite3_time_ms() - start_ms;
    printf("Operation elapsed: %llu ms\n", (unsigned long long)elapsed_ms);

    // Wall-clock & Timezone
    int64_t now_sec = sqlite3_time_now_sec();
    long tz_offset = sqlite3_time_timezone_offset_sec();
    printf("Epoch: %lld | TZ Offset: %ld seconds\n", (long long)now_sec, tz_offset);
}
```

---

## 3. Platform Implementation Details

| Platform | Monotonic Clock | Wall Clock | Sleep |
| :--- | :--- | :--- | :--- |
| **Windows** | `QueryPerformanceCounter` & `QueryPerformanceFrequency` | `GetSystemTimeAsFileTime` (converted to 1970 epoch) | `Sleep((DWORD)ms)` |
| **POSIX** | `clock_gettime(CLOCK_MONOTONIC)` | `clock_gettime(CLOCK_REALTIME)` / `gettimeofday` | `usleep` / `nanosleep` |
| **WASM / Fallback** | `gettimeofday` | `time(NULL)` / `gettimeofday` | Yield loop / no-op |
