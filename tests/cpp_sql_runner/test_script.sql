-- ============================================================================
-- SQL Script Runner: Macro Standalone Main Test
-- ============================================================================

-- %% Scenario 1: Setup & Insert Records
CREATE TABLE test_macro_items(
    id   INT PRIMARY KEY,
    name TEXT NOT NULL,
    val  REAL
);

INSERT INTO test_macro_items VALUES
    (1, 'Alpha', 10.5),
    (2, 'Beta',  20.0),
    (3, 'Gamma', 30.5);

-- %% Scenario 2: Query Validation
SELECT id, name, val FROM test_macro_items ORDER BY id;
-- @snapshot
-- | id | name  | val     |
-- |:---|:------|:--------|
-- | 1  | Alpha | 10.5000 |
-- | 2  | Beta  | 20.0000 |
-- | 3  | Gamma | 30.5000 |

-- %% Scenario 3: Aggregation & CTE
WITH Summary AS (
    SELECT count(*) AS total_items, sum(val) AS total_val FROM test_macro_items
)
SELECT total_items, total_val FROM Summary;
-- @snapshot
-- | total_items | total_val |
-- |:------------|:----------|
-- | 3           | 61.0000   |

-- %% Scenario 4: Clean Teardown
DROP TABLE test_macro_items;
