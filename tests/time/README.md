# Time & Clock Subsystem Test Suite (`tests/time`)

Comprehensive test suite verifying the zero-dependency high-resolution monotonic clock, wall-clock epoch timestamps, millisecond/microsecond sleep routines, and local timezone offset detection for SQLite extensions.

---

## 1. Test Suite Matrix

| Binary | Source File | Standard | Covered Subsystem | Test Cases |
| :--- | :--- | :--- | :--- | :---: |
| `test_time_c` | `test_time_c.c` | C99 | Pure C Clock, Sleep, & Timezone APIs | 4 |
| `test_time` | `test_time.cpp` | C++11 | C++ `SqliteClock` & `SqliteDuration` Wrappers | 5 |
| **Total** | **2 binaries** | | | **9 test cases** |

---

## 2. Test Specifications & Breakdown

### A. Pure C Time Suite (`test_time_c.c`)
1. **`test_monotonic_clock()`**:
   - *Objective*: Verifies nanosecond, microsecond, and millisecond monotonic timer precision.
   - *Mechanics*: Records start timestamps via `sqlite3_time_ns()`, `sqlite3_time_us()`, and `sqlite3_time_ms()`, performs a 20ms sleep via `sqlite3_time_sleep_ms(20)`, and asserts elapsed time is $\ge 15\text{ ms}$ with strictly increasing values.
2. **`test_wall_clock()`**:
   - *Objective*: Verifies Unix epoch wall-clock timestamp calculations.
   - *Mechanics*: Asserts that `sqlite3_time_now_sec()` and `sqlite3_time_now_ms()` return realistic timestamps aligned with the Unix epoch (`> 1700000000LL`).
3. **`test_sleep_precision()`**:
   - *Objective*: Tests microsecond sleep precision via `sqlite3_time_sleep_us(15000)`.
   - *Mechanics*: Asserts elapsed duration is $\ge 10\text{ ms}$ and $\le 100\text{ ms}$.
4. **`test_timezone_offset()`**:
   - *Objective*: Tests local system UTC timezone offset detection (`sqlite3_time_timezone_offset_sec()`).
   - *Mechanics*: Validates that the returned offset is within standard geographical boundaries ($-43200\text{ s}$ to $+50400\text{ s}$, i.e., UTC-12 to UTC+14) and matches a 15-minute alignment.

### B. C++11 `SqliteClock` & `SqliteDuration` (`test_time.cpp`)
1. **`test_cpp_monotonic_clock()`**:
   - *Objective*: Verifies `SqliteClock::now_ns()`, `now_us()`, and `now_ms()`.
2. **`test_cpp_wall_clock()`**:
   - *Objective*: Verifies `SqliteClock::wall_clock_sec()` and `wall_clock_ms()`.
3. **`test_cpp_sleep()`**:
   - *Objective*: Verifies `SqliteClock::sleep_for_ms()` and `SqliteClock::sleep_for_us()`.
4. **`test_cpp_timezone()`**:
   - *Objective*: Verifies `SqliteClock::timezone_offset_sec()`.
5. **`test_cpp_scoped_timer()`**:
   - *Objective*: Tests RAII scoped duration measurement (`SqliteScopedTimer`).

---

## 3. Compilation & Execution Guide

### Windows MSVC (`cl.exe`)
```cmd
cd tests\time
make.bat clean
make.bat build
make.bat test
```

### MSYS2 / GCC / Clang
```bash
cd /home/dilipvamsi/works/repos/sqlite-ext-core/tests/time
make clean
make all
make test
```
