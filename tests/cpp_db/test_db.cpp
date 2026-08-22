#define SQLITE_CORE
#include <stdio.h>
#include <assert.h>
#include <utility> // for std::move
#include <assert.h>
#include "sqlite3_db.hpp"
#include "sqlite3_transaction.hpp"

// SQLite custom VFS test is not needed, we can just use an in-memory DB or a temp file.
// Let's use a temp file to ensure it opens and closes successfully.

void test_database_owned() {
    printf("1. Testing SqliteDatabaseOwned RAII lifecycle...\n");
    
    const char* filename = "test_db.sqlite";
    
    {
        SqliteDatabaseOwned db(filename);
        assert(db); // db handle should not be null
        
        // Use the new statement builder
        auto stmt = db.prepare("CREATE TABLE test_table (id INT PRIMARY KEY);");
        assert(stmt.step() == SQLITE_DONE);
        
        auto stmt2 = db.prepare("INSERT INTO test_table (id) VALUES (42);");
        assert(stmt2.step() == SQLITE_DONE);
        
        // Destructor will automatically call sqlite3_close_v2
    }
    
    // Ensure the DB was closed by re-opening and checking the value is preserved
    {
        SqliteDatabaseOwned db(filename);
        auto stmt = db.prepare("SELECT id FROM test_table LIMIT 1;");
        assert(stmt.step() == SQLITE_ROW);
        assert(stmt.column_int(0) == 42);
    }
    
    remove(filename); // cleanup
}

void test_database_view_transactions() {
    printf("2. Testing SqliteDatabaseView interoperability with Transactions...\n");
    
    SqliteDatabaseOwned db(":memory:");
    assert(db);
    
    assert(db.prepare("CREATE TABLE test_table (id INT);").step() == SQLITE_DONE);
    
    {
        // Pass the database object into a transaction!
        SqliteTransaction txn(db);
        assert(txn);
        
        // Build statements directly from the transaction!
        auto stmt = txn.prepare("INSERT INTO test_table (id) VALUES (?);");
        stmt.bind(1, 99);
        assert(stmt.step() == SQLITE_DONE);
        
        {
            // Pass the database object into a savepoint!
            SqliteSavepoint sp(db, "my_savepoint");
            assert(sp);
            
            // Build statements directly from the transaction!
            auto stmt2 = txn.prepare("INSERT INTO test_table (id) VALUES (?);");
            stmt2.bind(1, 100);
            assert(stmt2.step() == SQLITE_DONE);
            
            assert(sp.rollback() == SQLITE_OK);
        }
        
        assert(txn.commit() == SQLITE_OK);
    }
    
    auto check = db.prepare("SELECT count(*) FROM test_table;");
    assert(check.step() == SQLITE_ROW);
    assert(check.column_int(0) == 1); // Only 99 should exist, 100 was rolled back
}

void test_database_exec() {
    printf("3. Testing SqliteDatabase exec() convenience helpers...\n");
    
    SqliteDatabaseOwned db(":memory:");
    
    // Test exec on Database
    assert(db.exec("CREATE TABLE exec_test (id INT);") == SQLITE_OK);
    
    // Test exec on Transaction
    {
        SqliteTransaction txn(db);
        assert(txn.exec("INSERT INTO exec_test VALUES (1), (2), (3);") == SQLITE_OK);
        assert(txn.commit() == SQLITE_OK);
    }
    
    auto stmt = db.prepare("SELECT sum(id) FROM exec_test;");
    assert(stmt.step() == SQLITE_ROW);
    assert(stmt.column_int(0) == 6);
}

void test_database_moves() {
    printf("4. Testing SqliteDatabaseOwned move semantics...\n");
    
    const char* filename = "test_db_moves.sqlite";
    
    {
        // 1. Creation
        SqliteDatabaseOwned db1(filename);
        assert(db1);
        db1.exec("CREATE TABLE move_table (val INT);");
        
        // 2. Move Constructor
        SqliteDatabaseOwned db2(std::move(db1));
        assert(!db1); // db1 should be empty
        assert(db2);  // db2 should own the handle
        
        db2.exec("INSERT INTO move_table VALUES (99);");
        
        // 3. Move Assignment
        SqliteDatabaseOwned db3(":memory:"); 
        db3 = std::move(db2);
        
        assert(!db2); // db2 should be empty
        assert(db3);  // db3 should now own the disk database
        
        auto stmt = db3.prepare("SELECT val FROM move_table;");
        assert(stmt.step() == SQLITE_ROW);
        assert(stmt.column_int(0) == 99);
    }
    // Destructor of db3 cleans up the handle. db1 and db2 destructors do nothing.
    
    remove(filename);
}

void test_database_exec_multiple() {
    printf("5. Testing SqliteDatabase exec() with multiple statements...\n");
    
    SqliteDatabaseOwned db(":memory:");
    
    // SQLite sqlite3_exec natively supports multiple semicolon-separated statements!
    assert(db.exec("CREATE TABLE multi_test (id INT); INSERT INTO multi_test VALUES (10); INSERT INTO multi_test VALUES (20);") == SQLITE_OK);
    
    auto stmt = db.prepare("SELECT sum(id) FROM multi_test;");
    assert(stmt.step() == SQLITE_ROW);
    assert(stmt.column_int(0) == 30);
}

void test_database_move_edge_cases() {
    printf("6. Testing SqliteDatabaseOwned move assignment edge cases...\n");
    
    SqliteDatabaseOwned db1(":memory:");
    
    // 1. Self-assignment (should be a no-op and not close the DB)
    SqliteDatabaseOwned& db_ref = db1;
    db1 = std::move(db_ref);
    assert(db1); // Should still be valid
    
    // 2. Moving from a null database
    // sqlite3_open_v2(nullptr) creates a temporary database, it does NOT return null.
    // To get a truly null handle, we must move out of it.
    SqliteDatabaseOwned temp(":memory:");
    SqliteDatabaseOwned dummy(std::move(temp));
    assert(!temp); // 'temp' is now explicitly null
    
    db1 = std::move(temp);
    assert(!db1); // db1 should now be null (its previous handle safely closed)
}

int main() {
    sqlite3_initialize();

    test_database_owned();
    test_database_view_transactions();
    test_database_exec();
    test_database_moves();
    test_database_exec_multiple();
    test_database_move_edge_cases();

    sqlite3_shutdown();

    printf("\nAll 6 SqliteDatabase Test Suites Passed Successfully!\n");
    return 0;
}
