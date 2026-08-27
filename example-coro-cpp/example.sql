-- ============================================================================
-- SQL Test Script for C++ Extension-Presence Shared Coroutine Pool
-- ============================================================================

.headers on
.mode box

-- 1. Load the compiled C++ extension
.load ./build/libcoro_cpp_example

SELECT '=============================================================' AS test_banner;
SELECT 'Testing Extension-Presence Shared Pool in SQLite (C++11/C++20)' AS test_title;
SELECT '=============================================================' AS test_banner;

-- 2. Check active database connection count
SELECT coro_cpp_ref_count() AS active_db_connections;

-- 3. Spawn 5 capturing lambda tasks representing Database Connection 1
SELECT coro_cpp_spawn(1, 1, 10) AS db1_t1,
       coro_cpp_spawn(1, 2, 10) AS db1_t2,
       coro_cpp_spawn(1, 3, 10) AS db1_t3,
       coro_cpp_spawn(1, 4, 10) AS db1_t4,
       coro_cpp_spawn(1, 5, 10) AS db1_t5;

-- 4. Spawn 5 capturing lambda tasks representing Database Connection 2
SELECT coro_cpp_spawn(2, 6, 10)  AS db2_t6,
       coro_cpp_spawn(2, 7, 10)  AS db2_t7,
       coro_cpp_spawn(2, 8, 10)  AS db2_t8,
       coro_cpp_spawn(2, 9, 10)  AS db2_t9,
       coro_cpp_spawn(2, 10, 10) AS db2_t10;

-- 5. Synchronously drain all tasks in the shared extension pool
SELECT coro_cpp_wait() AS synchronization_status;

-- 6. Inspect total tasks processed and global accumulated metrics
-- Each task i computes: (i * 10) + 200
-- Sum for 1..10 = (10+20+...+100) + (10 * 200) = 550 + 2000 = 2550
SELECT coro_cpp_tasks_completed() AS total_tasks_completed;
SELECT coro_cpp_global_sum()      AS global_accumulated_sum;
