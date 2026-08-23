-- ============================================================================
-- Pure C SQLite Extension Demo: Loading and testing ./build/libc_example
-- ============================================================================

-- 1. Load the compiled C extension
.load ./build/libc_example

.mode box
.header on

SELECT '=== 1. Pure C Stateless Scalar Function: c_math_hypot(3, 4) ===' AS test_banner;
SELECT 
    3.0 AS a, 
    4.0 AS b, 
    c_math_hypot(3.0, 4.0) AS hypotenuse, 
    c_math_hypot(6.0, 8.0) AS pythagorean_10;

SELECT '=== 2. Pure C Stateful Scalar Function: c_analytics_ping() ===' AS test_banner;
SELECT c_analytics_ping() AS query_count_1;
SELECT c_analytics_ping() AS query_count_2;
SELECT c_analytics_ping() AS query_count_3;

SELECT '=== 3. Pure C Custom Aggregate Function: c_sum_squares(val) ===' AS test_banner;
CREATE TEMP TABLE numbers(val INT);
INSERT INTO numbers VALUES (1), (2), (3), (4), (5);

SELECT 
    group_concat(val, ', ') AS inputs,
    c_sum_squares(val) AS sum_of_squares
FROM numbers;
