-- ============================================================================
-- Pure C Extension-Presence Shared Coroutine Pool: Feature & Snapshot Suite
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

-- Load the compiled Pure C coroutine extension shared library from build directory
.load ./build/libcoro_c_example
.mode box
.header on

-- %% Scenario 1: Verify Initial Pure C Pool Reference Count
SELECT coro_c_ref_count() AS active_db_connections;
-- @snapshot
-- | active_db_connections |
-- |:----------------------|
-- | 1                     |

-- %% Scenario 2: Dispatch Pure C Cooperative Worker Fibers (Connection 1 Batch)
SELECT 
    coro_c_spawn(1, 1, 10) AS db1_t1,
    coro_c_spawn(1, 2, 10) AS db1_t2,
    coro_c_spawn(1, 3, 10) AS db1_t3;
-- @snapshot
-- | db1_t1                   | db1_t2                   | db1_t3                   |
-- |:-------------------------|:-------------------------|:-------------------------|
-- | ENQUEUED_IN_EXTENSION_POOL | ENQUEUED_IN_EXTENSION_POOL | ENQUEUED_IN_EXTENSION_POOL |

-- %% Scenario 3: Dispatch Pure C Cooperative Worker Fibers (Connection 2 Batch)
SELECT 
    coro_c_spawn(2, 4, 10) AS db2_t4,
    coro_c_spawn(2, 5, 10) AS db2_t5;
-- @snapshot
-- | db2_t4                   | db2_t5                   |
-- |:-------------------------|:-------------------------|
-- | ENQUEUED_IN_EXTENSION_POOL | ENQUEUED_IN_EXTENSION_POOL |

-- %% Scenario 4: Synchronously Drain Shared Worker Pool Barrier
SELECT coro_c_wait() AS synchronization_status;
-- @snapshot
-- | synchronization_status |
-- |:-----------------------|
-- | EXTENSION_POOL_DRAINED |

-- %% Scenario 5: Inspect Pure C Process-Wide Accumulated Metrics
-- Tasks 1..5: item_ids 1,2,3,4,5 with multiplier 10 -> (10, 20, 30, 40, 50) + 100 each
-- Intermediate values: 110 + 120 + 130 + 140 + 150 = 650
SELECT 
    coro_c_tasks_completed() AS total_tasks_completed,
    coro_c_global_sum()      AS global_accumulated_sum;
-- @snapshot
-- | total_tasks_completed | global_accumulated_sum |
-- |:----------------------|:-----------------------|
-- | 5                     | 650                    |

-- %% Scenario 6: Secondary Batch Enqueue and Verification
SELECT coro_c_spawn(1, 6, 10) AS db1_t6;
-- @snapshot
-- | db1_t6                   |
-- |:-------------------------|
-- | ENQUEUED_IN_EXTENSION_POOL |

SELECT coro_c_wait() AS synchronization_status;
-- @snapshot
-- | synchronization_status |
-- |:-----------------------|
-- | EXTENSION_POOL_DRAINED |

-- Task 6: (6 * 10) + 100 = 160. Total sum = 650 + 160 = 810
SELECT 
    coro_c_tasks_completed() AS total_tasks_completed,
    coro_c_global_sum()      AS global_accumulated_sum;
-- @snapshot
-- | total_tasks_completed | global_accumulated_sum |
-- |:----------------------|:-----------------------|
-- | 6                     | 810                    |
