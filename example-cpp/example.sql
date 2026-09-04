-- ============================================================================
-- Modern C++ SQLite Extension: Comprehensive Feature & Snapshot Test Suite
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

-- Load the compiled extension shared library from build directory
.load ./build/libexample
.mode box
.header on

-- %% Scenario 1: Stateless Scalar Function (math_hypot)
SELECT 
    math_hypot(3.0, 4.0)  AS hypot_3_4,
    math_hypot(5.0, 12.0) AS hypot_5_12,
    math_hypot(0.0, 0.0)  AS hypot_zero;
-- @snapshot
-- | hypot_3_4 | hypot_5_12 | hypot_zero |
-- |:----------|:-----------|:-----------|
-- | 5.0000    | 13.0000    | 0.0000     |

SELECT math_hypot(8.0, 15.0) AS pythagorean_17;
-- @snapshot
-- | pythagorean_17 |
-- |:---------------|
-- | 17.0000        |

-- %% Scenario 2: Fallible String Function (text_repeat)
SELECT text_repeat('SQLite', 3) AS repeated_text;
-- @snapshot
-- | repeated_text |
-- |:--------------|
-- | SQLiteSQLiteSQLite |

-- %% Scenario 3: Per-Connection Stateful Scalar Function (analytics_ping)
SELECT analytics_ping() AS query_count_1;

-- @snapshot
-- | query_count_1 |
-- |:--------------|
-- | 1             |

SELECT analytics_ping() AS query_count_2;
-- @snapshot
-- | query_count_2 |
-- |:--------------|
-- | 2             |

SELECT analytics_ping() AS query_count_3;
-- @snapshot
-- | query_count_3 |
-- |:--------------|
-- | 3             |

-- %% Scenario 4: Object-Oriented Aggregate Function (geo_mean)
CREATE TEMP TABLE sample_numbers(val REAL);
INSERT INTO sample_numbers VALUES (2.0), (8.0), (32.0);

SELECT 
    count(*) AS item_count,
    geo_mean(val) AS geometric_mean
FROM sample_numbers;
-- @snapshot
-- | item_count | geometric_mean |
-- |:-----------|:---------------|
-- | 3          | 8.0000         |

INSERT INTO sample_numbers VALUES (128.0), (512.0);

SELECT 
    count(*) AS item_count,
    geo_mean(val) AS geometric_mean
FROM sample_numbers;
-- @snapshot
-- | item_count | geometric_mean |
-- |:-----------|:---------------|
-- | 5          | 32.0000        |

-- %% Scenario 5: Table-Valued Function (fibonacci)
SELECT 
    idx AS term, 
    val AS fibonacci_number 
FROM fibonacci(8);
-- @snapshot
-- | term | fibonacci_number |
-- |:-----|:-----------------|
-- | 1    | 1                |
-- | 2    | 1                |
-- | 3    | 2                |
-- | 4    | 3                |
-- | 5    | 5                |
-- | 6    | 8                |
-- | 7    | 13               |
-- | 8    | 21               |

SELECT 
    count(*) AS total_terms,
    max(val) AS max_fibonacci_val
FROM fibonacci(12);
-- @snapshot
-- | total_terms | max_fibonacci_val |
-- |:------------|:------------------|
-- | 12          | 144               |

-- %% Scenario 6: CTE and Relational Filtering with TVF
WITH EvenFibs AS (
    SELECT idx, val 
    FROM fibonacci(10) 
    WHERE val % 2 = 0
)
SELECT idx AS even_term_idx, val AS even_val 
FROM EvenFibs 
ORDER BY val;
-- @snapshot
-- | even_term_idx | even_val |
-- |:--------------|:---------|
-- | 3             | 2        |
-- | 6             | 8        |
-- | 9             | 34       |

-- %% Scenario 7: Clean Teardown
DROP TABLE sample_numbers;

