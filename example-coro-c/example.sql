-- ============================================================================
-- SQL Test Script for Pure C Extension-Presence Shared Coroutine Pool
-- ============================================================================

.headers on
.mode box

-- 1. Load the compiled extension into Database Connection 1
.load ./build/libcoro_c_example

SELECT '===========================================================' AS test_banner;
SELECT 'Testing Extension-Presence Shared Pool in SQLite (Pure C)' AS test_title;
SELECT '===========================================================' AS test_banner;

-- 2. Spawn 5 tasks representing Database Connection 1
SELECT coro_c_spawn(1, 1, 10) AS db1_t1,
       coro_c_spawn(1, 2, 10) AS db1_t2,
       coro_c_spawn(1, 3, 10) AS db1_t3,
       coro_c_spawn(1, 4, 10) AS db1_t4,
       coro_c_spawn(1, 5, 10) AS db1_t5;

-- 3. Spawn 5 tasks representing Database Connection 2 sharing the same pool
SELECT coro_c_spawn(2, 6, 10)  AS db2_t6,
       coro_c_spawn(2, 7, 10)  AS db2_t7,
       coro_c_spawn(2, 8, 10)  AS db2_t8,
       coro_c_spawn(2, 9, 10)  AS db2_t9,
       coro_c_spawn(2, 10, 10) AS db2_t10;

-- 4. Synchronously drain all tasks in the shared extension pool
SELECT coro_c_wait() AS synchronization_status;

-- 5. Verify total tasks processed and global accumulated metrics
SELECT coro_c_tasks_completed() AS total_tasks_completed;
SELECT coro_c_global_sum()      AS global_accumulated_sum;
