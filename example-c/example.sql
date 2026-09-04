-- ============================================================================
-- Pure C (C99/C11) SQLite Extension: Comprehensive Feature & Snapshot Suite
-- ============================================================================
-- EXECUTION & CELL STRUCTURE:
-- This script is organized into interactive execution cells delimited by:
--   `-- %% <Cell Title>`
--
-- Query results are validated against Markdown snapshots:
--   `-- @snapshot`
--   `-- | col1 | col2 | ... |`
--   `-- |:---|:---| ... |`
-- ============================================================================

-- Load the compiled Pure C extension shared library from build directory
.load ./build/libc_example
.mode box
.header on

-- %% Scenario 1: Pure C Stateless Scalar Function (c_math_hypot)
SELECT 
    c_math_hypot(3.0, 4.0) AS hypot_3_4,
    c_math_hypot(6.0, 8.0) AS hypot_6_8,
    c_math_hypot(0.0, 0.0) AS hypot_zero;
-- @snapshot
-- | hypot_3_4 | hypot_6_8 | hypot_zero |
-- |:----------|:----------|:-----------|
-- | 5.0000    | 10.0000   | 0.0000     |

SELECT c_math_hypot(5.0, 12.0) AS pythagorean_13;
-- @snapshot
-- | pythagorean_13 |
-- |:---------------|
-- | 13.0000        |

-- %% Scenario 2: Pure C Stateful Scalar Function (c_analytics_ping)
SELECT c_analytics_ping() AS query_count_1;
-- @snapshot
-- | query_count_1 |
-- |:--------------|
-- | 1             |

SELECT c_analytics_ping() AS query_count_2;
-- @snapshot
-- | query_count_2 |
-- |:--------------|
-- | 2             |

SELECT c_analytics_ping() AS query_count_3;
-- @snapshot
-- | query_count_3 |
-- |:--------------|
-- | 3             |

-- %% Scenario 3: Pure C Custom Aggregate Function (c_sum_squares)
CREATE TEMP TABLE numbers(val INT);
INSERT INTO numbers VALUES (1), (2), (3), (4), (5);

SELECT 
    count(*) AS total_items,
    c_sum_squares(val) AS sum_of_squares
FROM numbers;
-- @snapshot
-- | total_items | sum_of_squares |
-- |:------------|:---------------|
-- | 5           | 55             |

INSERT INTO numbers VALUES (6), (7);

SELECT 
    count(*) AS total_items,
    c_sum_squares(val) AS sum_of_squares
FROM numbers;
-- @snapshot
-- | total_items | sum_of_squares |
-- |:------------|:---------------|
-- | 7           | 140            |

-- %% Scenario 4: Clean Teardown
DROP TABLE numbers;
