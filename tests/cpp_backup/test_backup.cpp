#include <stdio.h>
#include <assert.h>
#include <sqlite3.h>
#include "../../include/sqlite3_backup.hpp"

// Utility function to execute a query
static void exec(sqlite3* db, const char* sql) {
    char* err = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        printf("SQL error: %s\n", err);
        sqlite3_free(err);
    }
    assert(rc == SQLITE_OK);
}

// Utility function to count rows
static int count_rows(sqlite3* db, const char* table_name) {
    char query[256];
    snprintf(query, sizeof(query), "SELECT COUNT(*) FROM %s;", table_name);
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, query, -1, &stmt, nullptr);
    assert(rc == SQLITE_OK);
    
    rc = sqlite3_step(stmt);
    assert(rc == SQLITE_ROW);
    
    int count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    
    return count;
}

void test_backup() {
    printf("Testing SqliteBackup API...\n");

    sqlite3* src_db;
    sqlite3* dest_db;

    assert(sqlite3_open(":memory:", &src_db) == SQLITE_OK);
    assert(sqlite3_open(":memory:", &dest_db) == SQLITE_OK);

    // 1. Populate source database
    exec(src_db, "CREATE TABLE data (id INTEGER PRIMARY KEY, val TEXT);");
    exec(src_db, "BEGIN TRANSACTION;");
    for (int i = 0; i < 1000; i++) {
        exec(src_db, "INSERT INTO data(val) VALUES ('test_row_data');");
    }
    exec(src_db, "COMMIT;");

    // 2. Perform the backup
    {
        SqliteBackup backup(dest_db, "main", src_db, "main");
        assert(backup); // Should be successfully initialized

        // Back up 5 pages at a time to test the step loop
        int rc;
        do {
            rc = backup.step(5);
            int remaining = backup.remaining();
            int pagecount = backup.pagecount();
            assert(remaining >= 0);
            assert(pagecount > 0);
            assert(remaining <= pagecount);
        } while (rc == SQLITE_OK);

        assert(rc == SQLITE_DONE);
        
        // Destructor handles sqlite3_backup_finish cleanly
    }

    // 3. Verify destination database has all 1000 rows
    assert(count_rows(dest_db, "data") == 1000);
    
    // 4. Test Move Semantics
    {
        SqliteBackup backup1(dest_db, "main", src_db, "main");
        assert(backup1);
        
        SqliteBackup backup2 = static_cast<SqliteBackup&&>(backup1);
        assert(!backup1); // Moved from
        assert(backup2);  // Takes ownership
        
        // Test move assignment
        SqliteBackup backup3;
        backup3 = static_cast<SqliteBackup&&>(backup2);
        assert(!backup2);
        assert(backup3);
        
        backup3.step(-1); // Copy all remaining pages in one go
    }

    sqlite3_close(dest_db);
    sqlite3_close(src_db);
    
    printf("SqliteBackup passed successfully!\n");
}

int main() {
    test_backup();
    return 0;
}
