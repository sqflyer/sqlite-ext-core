# Time & Clock Architecture (`sqlite3_time.h` / `sqlite3_time.hpp`)

This document details the internal design, cross-platform hardware timer abstractions, Unix epoch conversions, and timezone calculation models implemented by `sqlite3_time.h` and `sqlite3_time.hpp`.

---

## 1. Zero-Dependency Clock Design

In SQLite extensions, measuring execution duration and timestamping events is critical for query profiling, cron engines, rate limiters, and timeout handlers. However, including `<chrono>` brings in massive C++ standard library dependencies that can trigger runtime symbol conflicts or bloat shared libraries.

`sqlite-ext-core` decouples time from the standard library by directly wrapping native operating system hardware timers:

### Monotonic Timing Pipeline
- **Windows**: Uses Win32 High-Resolution Performance Counters (`QueryPerformanceCounter` & `QueryPerformanceFrequency`). Integer division against frequency yields exact nanosecond and microsecond measurements without floating-point overhead.
- **POSIX (Linux / macOS / BSD)**: Uses `clock_gettime(CLOCK_MONOTONIC)` with fallback to `gettimeofday()`.
- **Properties**: Strictly monotonically increasing; immune to manual system clock adjustments, NTP step adjustments, and leap seconds.

```c
uint64_t start_ns = sqlite3_time_ns();
// ... execute database query ...
uint64_t elapsed_us = (sqlite3_time_ns() - start_ns) / 1000ULL;
```

---

## 2. Unix Epoch Wall-Clock & Timezone Alignment

### Wall-Clock Time
- `sqlite3_time_now_sec()`: Returns seconds elapsed since January 1, 1970 00:00:00 UTC.
- `sqlite3_time_now_ms()`: Returns milliseconds elapsed since Unix epoch.
- **Windows Translation**: `GetSystemTimeAsFileTime` returns 100-nanosecond intervals since January 1, 1601. We translate this to Unix epoch via the integer constant offset `116444736000000000ULL`:
  $$\text{Unix ms} = \frac{\text{FileTime} - 116444736000000000\text{ULL}}{10000\text{ULL}}$$
- **POSIX Translation**: Uses `clock_gettime(CLOCK_REALTIME)` with millisecond arithmetic:
  $$\text{Unix ms} = (\text{tv\_sec} \times 1000) + \frac{\text{tv\_nsec}}{1000000}$$

### Timezone Offset Calculation
Detects the local system's UTC offset in seconds without spawning shell processes or querying external timezone database files:
- **Linux / macOS / BSD**: Reads `struct tm::tm_gmtoff` populated by thread-safe `localtime_r(&now, &tm_local)`.
- **Windows**: Computes `difftime(mktime(&tm_local), mktime(&tm_utc))` using `localtime_s` and `gmtime_s`.
- **Properties**: Automatically responds to Daylight Saving Time (DST) changes.

---

## 3. High-Resolution Sleeping Routines

Sleeping routines provide accurate thread suspensions without bringing in `<thread>` or `<chrono>`:
- **`sqlite3_time_sleep_ms(ms)`**:
  - Windows: `Sleep((DWORD)ms)`.
  - POSIX: Uses `nanosleep()` with `struct timespec { ms / 1000, (ms % 1000) * 1000000 }` to avoid deprecated `usleep()` limitations under strict C99.
- **`sqlite3_time_sleep_us(us)`**:
  - Windows: `Sleep((DWORD)(us / 1000))` (or `Sleep(0)` for sub-millisecond yielding).
  - POSIX: Uses `nanosleep()` with `struct timespec { us / 1000000, (us % 1000000) * 1000 }`.

---

## 4. C++11 RAII Clock API (`sqlite3_time.hpp`)

`sqlite3_time.hpp` provides clean C++11 abstractions:
- **`SqliteClock`**: Static utility class offering `now_ns()`, `now_us()`, `now_ms()`, `wall_clock_sec()`, and `timezone_offset_sec()`.
- **`SqliteScopedTimer`**: RAII timer measuring execution duration between scope entry and exit.
