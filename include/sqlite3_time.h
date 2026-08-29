#ifndef SQLITE3_TIME_H
#define SQLITE3_TIME_H

/**
 * @file sqlite3_time.h
 * @brief High-precision, zero-dependency clock, sleep, and timezone subsystem for SQLite extensions.
 *
 * Provides cross-platform monotonic timers, wall-clock epoch timestamps, millisecond/microsecond
 * sleeping routines, and automatic system timezone offset detection without external dependencies.
 * Compatible with C99, C11, C++11, -nostdlib, and -nostdlib++.
 */

#if !defined(_WIN32) && !defined(_WIN64)
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include <stdint.h>
#include <time.h>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Retrieves the current monotonic clock timestamp in nanoseconds.
 * 
 * Monotonic time is guaranteed to be strictly increasing and is immune to
 * manual system clock modifications or NTP step adjustments.
 * 
 * @return Monotonic time in nanoseconds.
 */
static inline uint64_t sqlite3_time_ns(void) {
#if defined(_WIN32) || defined(_WIN64)
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000000000ULL) / freq.QuadPart);
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((uint64_t)tv.tv_sec * 1000000000ULL) + ((uint64_t)tv.tv_usec * 1000ULL);
#endif
}

/**
 * @brief Retrieves the current monotonic clock timestamp in microseconds.
 * 
 * @return Monotonic time in microseconds.
 */
static inline uint64_t sqlite3_time_us(void) {
    return sqlite3_time_ns() / 1000ULL;
}

/**
 * @brief Retrieves the current monotonic clock timestamp in milliseconds.
 * 
 * @return Monotonic time in milliseconds.
 */
static inline uint64_t sqlite3_time_ms(void) {
    return sqlite3_time_ns() / 1000000ULL;
}

/**
 * @brief Retrieves the current Unix epoch wall-clock timestamp in seconds.
 * 
 * Represents seconds elapsed since January 1, 1970 00:00:00 UTC.
 * 
 * @return Wall-clock time in seconds.
 */
static inline int64_t sqlite3_time_now_sec(void) {
    return (int64_t)time(NULL);
}

/**
 * @brief Retrieves the current Unix epoch wall-clock timestamp in milliseconds.
 * 
 * Represents milliseconds elapsed since January 1, 1970 00:00:00 UTC.
 * 
 * @return Wall-clock time in milliseconds.
 */
static inline int64_t sqlite3_time_now_ms(void) {
#if defined(_WIN32) || defined(_WIN64)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    // Difference between Windows epoch (Jan 1 1601) and Unix epoch (Jan 1 1970) in 100-ns intervals:
    return (int64_t)((uli.QuadPart - 116444736000000000ULL) / 10000ULL);
#elif defined(CLOCK_REALTIME)
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((int64_t)ts.tv_sec * 1000LL) + ((int64_t)ts.tv_nsec / 1000000LL);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((int64_t)tv.tv_sec * 1000LL) + ((int64_t)tv.tv_usec / 1000LL);
#endif
}

/**
 * @brief Computes the local system timezone offset from UTC in seconds.
 * 
 * Returns positive seconds for locations East of UTC (e.g. +19800 for UTC+5:30)
 * and negative seconds for locations West of UTC. Automatically accounts for
 * active Daylight Saving Time (DST).
 * 
 * @return Timezone offset from UTC in seconds.
 */
static inline long sqlite3_time_timezone_offset_sec(void) {
    time_t now = time(NULL);
    struct tm tm_local;
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&tm_local, &now);
#else
    localtime_r(&now, &tm_local);
#endif

#if (defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)) && (defined(__USE_MISC) || defined(_GNU_SOURCE) || defined(_DEFAULT_SOURCE) || defined(_BSD_SOURCE))
    return tm_local.tm_gmtoff;
#else
    struct tm tm_utc;
#if defined(_WIN32) || defined(_WIN64)
    gmtime_s(&tm_utc, &now);
#else
    gmtime_r(&now, &tm_utc);
#endif
    time_t t_local = mktime(&tm_local);
    time_t t_utc = mktime(&tm_utc);
    return (long)difftime(t_local, t_utc);
#endif
}

/**
 * @brief Suspends the execution of the calling thread for the specified duration.
 * 
 * @param ms Duration to sleep in milliseconds.
 */
static inline void sqlite3_time_sleep_ms(unsigned int ms) {
#if defined(_WIN32) || defined(_WIN64)
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000U);
    ts.tv_nsec = (long)((ms % 1000U) * 1000000L);
    nanosleep(&ts, NULL);
#endif
}

/**
 * @brief Suspends the execution of the calling thread for the specified duration.
 * 
 * @param us Duration to sleep in microseconds.
 */
static inline void sqlite3_time_sleep_us(unsigned int us) {
#if defined(_WIN32) || defined(_WIN64)
    if (us >= 1000U) {
        Sleep((DWORD)(us / 1000U));
    } else if (us > 0) {
        Sleep(0);
    }
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(us / 1000000U);
    ts.tv_nsec = (long)((us % 1000000U) * 1000L);
    nanosleep(&ts, NULL);
#endif
}

#ifdef __cplusplus
}
#endif

#endif // SQLITE3_TIME_H
