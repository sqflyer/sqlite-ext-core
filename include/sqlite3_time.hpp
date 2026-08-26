#ifndef SQLITE3_TIME_HPP
#define SQLITE3_TIME_HPP

/**
 * @file sqlite3_time.hpp
 * @brief High-precision, zero-dependency C++11 clock, stopwatch, and timezone utilities.
 * 
 * Compliant with -nostdlib++ and -fno-exceptions (zero dependency on <chrono>).
 */

#include "sqlite3_time.h"

/**
 * @class SqliteClock
 * @brief Static utility providing high-resolution monotonic and wall-clock timestamps.
 */
class SqliteClock {
public:
    /**
     * @brief Retrieves the current monotonic timestamp in nanoseconds.
     * @return Monotonic time in nanoseconds.
     */
    static inline uint64_t monotonic_ns() {
        return sqlite3_time_ns();
    }

    /**
     * @brief Retrieves the current monotonic timestamp in microseconds.
     * @return Monotonic time in microseconds.
     */
    static inline uint64_t monotonic_us() {
        return sqlite3_time_us();
    }

    /**
     * @brief Retrieves the current monotonic timestamp in milliseconds.
     * @return Monotonic time in milliseconds.
     */
    static inline uint64_t monotonic_ms() {
        return sqlite3_time_ms();
    }

    /**
     * @brief Retrieves the Unix epoch wall-clock timestamp in seconds.
     * @return Wall-clock time in seconds.
     */
    static inline int64_t now_sec() {
        return sqlite3_time_now_sec();
    }

    /**
     * @brief Retrieves the Unix epoch wall-clock timestamp in milliseconds.
     * @return Wall-clock time in milliseconds.
     */
    static inline int64_t now_ms() {
        return sqlite3_time_now_ms();
    }

    /**
     * @brief Retrieves the system timezone offset from UTC in seconds.
     * @return Timezone offset in seconds.
     */
    static inline long timezone_offset_sec() {
        return sqlite3_time_timezone_offset_sec();
    }

    /**
     * @brief Pauses execution of the calling thread for the given milliseconds.
     * @param ms Duration in milliseconds.
     */
    static inline void sleep_for_ms(unsigned int ms) {
        sqlite3_time_sleep_ms(ms);
    }

    /**
     * @brief Pauses execution of the calling thread for the given microseconds.
     * @param us Duration in microseconds.
     */
    static inline void sleep_for_us(unsigned int us) {
        sqlite3_time_sleep_us(us);
    }
};

/**
 * @class SqliteStopwatch
 * @brief Zero-overhead RAII stopwatch for measuring execution time and benchmarking queries.
 */
class SqliteStopwatch {
private:
    uint64_t m_start_ns;

public:
    /**
     * @brief Constructs and starts the stopwatch timer immediately.
     */
    inline SqliteStopwatch() : m_start_ns(sqlite3_time_ns()) {}

    /**
     * @brief Resets the stopwatch start time to the current monotonic timestamp.
     */
    inline void restart() {
        m_start_ns = sqlite3_time_ns();
    }

    /**
     * @brief Returns the elapsed time since start/restart in nanoseconds.
     * @return Elapsed time in nanoseconds.
     */
    inline uint64_t elapsed_ns() const {
        uint64_t now = sqlite3_time_ns();
        return (now >= m_start_ns) ? (now - m_start_ns) : 0ULL;
    }

    /**
     * @brief Returns the elapsed time since start/restart in microseconds.
     * @return Elapsed time in microseconds.
     */
    inline uint64_t elapsed_us() const {
        return elapsed_ns() / 1000ULL;
    }

    /**
     * @brief Returns the elapsed time since start/restart in milliseconds.
     * @return Elapsed time in milliseconds.
     */
    inline uint64_t elapsed_ms() const {
        return elapsed_ns() / 1000000ULL;
    }

    /**
     * @brief Returns the elapsed time since start/restart in fractional seconds.
     * @return Elapsed time in seconds as a floating-point value.
     */
    inline double elapsed_sec() const {
        return static_cast<double>(elapsed_ns()) / 1000000000.0;
    }
};

/**
 * @class SqliteTimezone
 * @brief Timezone calculation and offset decomposition utilities.
 */
class SqliteTimezone {
public:
    /**
     * @brief Local offset from UTC in seconds (e.g. +19800 for UTC+5:30).
     * @return Offset in seconds.
     */
    static inline long offset_seconds() {
        return sqlite3_time_timezone_offset_sec();
    }

    /**
     * @brief Total hours component of the local timezone offset.
     * @return Signed integer representing the offset hours.
     */
    static inline int offset_hours() {
        return static_cast<int>(offset_seconds() / 3600L);
    }

    /**
     * @brief Minutes component (0..59) of the local timezone offset.
     * @return Integer between 0 and 59 representing the offset minutes.
     */
    static inline int offset_minutes() {
        long sec = offset_seconds();
        if (sec < 0) sec = -sec;
        return static_cast<int>((sec % 3600L) / 60L);
    }
};

#endif // SQLITE3_TIME_HPP
