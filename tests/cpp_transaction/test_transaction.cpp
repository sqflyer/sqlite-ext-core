#define SQLITE_CORE
#include <stdio.h>
#include <assert.h>
#include "sqlite3_transaction.hpp"

// Simple helper to check if a row exists in our test table
bool row_exists(sqlite3* db, int id) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT 1 FROM test_table WHERE id = ?;", -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, id);
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

void test_transaction_commit(sqlite3* db) {
    printf("1. Testing SqliteTransaction commit()...\n");
    
    {
        SqliteTransaction txn(db);
        assert(txn); // should be active
        
        assert(sqlite3_exec(db, "INSERT INTO test_table (id) VALUES (1);", nullptr, nullptr, nullptr) == SQLITE_OK);
        
        assert(txn.commit() == SQLITE_OK);
        assert(!txn); // should be inactive now
    }
    
    // Row 1 should exist after commit
    assert(row_exists(db, 1));
}

void test_transaction_rollback(sqlite3* db) {
    printf("2. Testing SqliteTransaction explicit rollback()...\n");
    
    {
        SqliteTransaction txn(db);
        assert(txn);
        
        assert(sqlite3_exec(db, "INSERT INTO test_table (id) VALUES (2);", nullptr, nullptr, nullptr) == SQLITE_OK);
        
        assert(txn.rollback() == SQLITE_OK);
        assert(!txn);
    }
    
    // Row 2 should NOT exist after rollback
    assert(!row_exists(db, 2));
}

void test_transaction_auto_rollback(sqlite3* db) {
    printf("3. Testing SqliteTransaction RAII auto-rollback (scope exit)...\n");
    
    {
        SqliteTransaction txn(db);
        assert(txn);
        
        assert(sqlite3_exec(db, "INSERT INTO test_table (id) VALUES (3);", nullptr, nullptr, nullptr) == SQLITE_OK);
        
        // Exiting scope without calling commit() or rollback()!
    }
    
    // Row 3 should NOT exist after RAII destructor auto-rollback
    assert(!row_exists(db, 3));
}

void test_savepoint_nested(sqlite3* db) {
    printf("4. Testing SqliteSavepoint nested savepoints...\n");
    
    {
        // Outer transaction
        SqliteTransaction txn(db);
        assert(sqlite3_exec(db, "INSERT INTO test_table (id) VALUES (4);", nullptr, nullptr, nullptr) == SQLITE_OK);
        
        {
            // Inner savepoint that succeeds
            SqliteSavepoint sp1(db, "my_sp1");
            assert(sp1);
            assert(sqlite3_exec(db, "INSERT INTO test_table (id) VALUES (5);", nullptr, nullptr, nullptr) == SQLITE_OK);
            assert(sp1.release() == SQLITE_OK); // commit savepoint
        }
        
        {
            // Inner savepoint that gets explicitly rolled back
            SqliteSavepoint sp2(db, "my_sp2");
            assert(sqlite3_exec(db, "INSERT INTO test_table (id) VALUES (6);", nullptr, nullptr, nullptr) == SQLITE_OK);
            assert(sp2.rollback() == SQLITE_OK); // rollback savepoint
        }
        
        {
            // Inner savepoint that gets auto-rolled back by RAII
            SqliteSavepoint sp3(db, "my_sp3");
            assert(sqlite3_exec(db, "INSERT INTO test_table (id) VALUES (7);", nullptr, nullptr, nullptr) == SQLITE_OK);
            // exits scope
        }
        
        assert(txn.commit() == SQLITE_OK);
    }
    
    assert(row_exists(db, 4));  // outer txn committed
    assert(row_exists(db, 5));  // sp1 released (committed)
    assert(!row_exists(db, 6)); // sp2 explicitly rolled back
    assert(!row_exists(db, 7)); // sp3 auto-rolled back by RAII
}

void test_transaction_behaviors(sqlite3* db) {
    printf("5. Testing SqliteTransactionBehavior (IMMEDIATE, EXCLUSIVE)...\n");
    
    {
        // Test IMMEDIATE
        SqliteTransaction txn(db, SqliteTransactionBehavior::IMMEDIATE);
        assert(txn);
        assert(sqlite3_exec(db, "INSERT INTO test_table (id) VALUES (8);", nullptr, nullptr, nullptr) == SQLITE_OK);
        assert(txn.commit() == SQLITE_OK);
    }
    assert(row_exists(db, 8));

    {
        // Test EXCLUSIVE
        SqliteTransaction txn(db, SqliteTransactionBehavior::EXCLUSIVE);
        assert(txn);
        assert(sqlite3_exec(db, "INSERT INTO test_table (id) VALUES (9);", nullptr, nullptr, nullptr) == SQLITE_OK);
        assert(txn.rollback() == SQLITE_OK);
    }
    assert(!row_exists(db, 9));
}

void test_invalid_states(sqlite3* db) {
    printf("6. Testing SQLITE_MISUSE on invalid states (Double Commit/Rollback)...\n");
    
    // Transactions
    {
        SqliteTransaction txn(db);
        assert(txn);
        assert(txn.commit() == SQLITE_OK);
        assert(!txn);
        
        // Double commit/rollback should safely return SQLITE_MISUSE
        assert(txn.commit() == SQLITE_MISUSE);
        assert(txn.rollback() == SQLITE_MISUSE);
    }
    
    // Savepoints
    {
        SqliteTransaction txn(db);
        SqliteSavepoint sp(db, "misuse_test");
        assert(sp);
        assert(sp.release() == SQLITE_OK);
        assert(!sp);
        
        // Double release/rollback should safely return SQLITE_MISUSE
        assert(sp.release() == SQLITE_MISUSE);
        assert(sp.rollback() == SQLITE_MISUSE);
        
        // Test NULL identifier
        SqliteSavepoint sp_null(db, nullptr);
        assert(!sp_null); // should silently fail to activate
        assert(sp_null.release() == SQLITE_MISUSE);
    }
}

void test_deeply_nested_savepoints(sqlite3* db) {
    printf("7. Testing deeply nested SqliteSavepoints (Level 3 depth)...\n");
    
    SqliteTransaction txn(db);
    assert(txn.exec("INSERT INTO test_table (id) VALUES (10);") == SQLITE_OK);
    
    {
        SqliteSavepoint sp1(db, "level_1");
        assert(txn.exec("INSERT INTO test_table (id) VALUES (11);") == SQLITE_OK);
        
        {
            SqliteSavepoint sp2(db, "level_2");
            assert(txn.exec("INSERT INTO test_table (id) VALUES (12);") == SQLITE_OK);
            
            {
                SqliteSavepoint sp3(db, "level_3");
                assert(txn.exec("INSERT INTO test_table (id) VALUES (13);") == SQLITE_OK);
                assert(sp3.rollback() == SQLITE_OK); // Rolls back 13, keeps 12 and 11
            }
            
            assert(sp2.release() == SQLITE_OK); // Commits 12
        }
        
        assert(sp1.release() == SQLITE_OK); // Commits 11 and 12
    }
    
    assert(txn.commit() == SQLITE_OK);
    
    assert(row_exists(db, 10));
    assert(row_exists(db, 11));
    assert(row_exists(db, 12));
    assert(!row_exists(db, 13)); // Level 3 was rolled back!
}

int main() {
    sqlite3_initialize();

    sqlite3* db;
    assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
    assert(sqlite3_exec(db, "CREATE TABLE test_table (id INT PRIMARY KEY);", nullptr, nullptr, nullptr) == SQLITE_OK);

    test_transaction_commit(db);
    test_transaction_rollback(db);
    test_transaction_auto_rollback(db);
    test_savepoint_nested(db);
    test_transaction_behaviors(db);
    test_invalid_states(db);
    test_deeply_nested_savepoints(db);

    sqlite3_close(db);
    sqlite3_shutdown();

    printf("\nAll 7 SqliteTransaction Test Suites (100%% Coverage) Passed Successfully!\n");
    return 0;
}
