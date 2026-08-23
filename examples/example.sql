-- ============================================================================
-- SQLite Extension Demo: Loading and testing ./build/libexample
-- ============================================================================

-- 1. Load the compiled extension shared library from build directory
.load ./build/libexample

.mode box
.header on

SELECT '=== 1. Stateless Scalar Function: math_hypot(3, 4) ===' AS test_banner;
SELECT 
    3.0 AS a, 
    4.0 AS b, 
    math_hypot(3.0, 4.0) AS hypotenuse, 
    math_hypot(5.0, 12.0) AS pythagorean_13;

SELECT '=== 2. Stateful Scalar Function: analytics_ping() ===' AS test_banner;
SELECT analytics_ping() AS query_count_1;
SELECT analytics_ping() AS query_count_2;
SELECT analytics_ping() AS query_count_3;

SELECT '=== 3. Custom Aggregate Function: geo_mean(x) ===' AS test_banner;
CREATE TEMP TABLE sample_numbers(val REAL);
INSERT INTO sample_numbers VALUES (2.0), (8.0), (32.0);

SELECT 
    group_concat(val, ', ') AS inputs,
    geo_mean(val) AS geometric_mean
FROM sample_numbers;

SELECT '=== 4. Table-Valued Function: fibonacci(8) ===' AS test_banner;
SELECT 
    idx AS term, 
    val AS fibonacci_number 
FROM fibonacci(8);
