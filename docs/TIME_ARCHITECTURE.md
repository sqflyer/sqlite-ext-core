# Time & Clock Architecture

## 1. Zero-Dependency Clock Design

In SQLite extensions, measuring execution duration and timestamping events is critical for query profiling, cron engines, and timeout handlers. However, including `<chrono>` brings in massive C++ standard library dependencies that can trigger runtime symbol conflicts or bloat shared libraries.

`sqlite-ext-core` decouples time from the standard library by directly wrapping native operating system hardware timers:

### Monotonic Timing Pipeline
- **Windows**: Uses Win32 High-Resolution Performance Counters (`QueryPerformanceCounter`). We calculate integer division against `QueryPerformanceFrequency` to produce exact nanosecond/microsecond measurements without floating-point overhead.
- **POSIX (Linux / macOS / BSD)**: Uses `clock_gettime(CLOCK_MONOTONIC)`.
- **Properties**: Strictly monotonically increasing; immune to clock jumps, NTP sync steps, and leap seconds.

---

## 2. Unix Epoch & Timezone Alignment

### Wall-Clock Time
- `sqlite3_time_now_sec()` and `sqlite3_time_now_ms()` return Unix epoch time (elapsed seconds/milliseconds since `1970-01-01T00:00:00Z`).
- On Windows, `GetSystemTimeAsFileTime` measures 100-nanosecond intervals since January 1, 1601. We translate this to Unix epoch via the constant offset `116444736000000000ULL`.

### Timezone Offset Calculation
- Detects the local system's UTC offset without spawning shell processes or querying external files.
- On Linux/macOS, reads `struct tm::tm_gmtoff` populated by `localtime_r`.
- On Windows, computes `difftime(mktime(localtime), mktime(gmtime))`.
- Automatically responds to Daylight Saving Time (DST) changes.
