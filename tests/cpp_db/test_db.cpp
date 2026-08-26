#define SQLITE_CORE
#include <stdio.h>
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
        SqliteDatabaseOwned db2(sqlite_move(db1));
        assert(!db1); // db1 should be empty
        assert(db2);  // db2 should own the handle
        
        db2.exec("INSERT INTO move_table VALUES (99);");
        
        // 3. Move Assignment
        SqliteDatabaseOwned db3(":memory:"); 
        db3 = sqlite_move(db2);
        
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
    db1 = sqlite_move(db_ref);
    assert(db1); // Should still be valid
    
    // 2. Moving from a null database
    // sqlite3_open_v2(nullptr) creates a temporary database, it does NOT return null.
    // To get a truly null handle, we must move out of it.
    SqliteDatabaseOwned temp(":memory:");
    SqliteDatabaseOwned dummy(sqlite_move(temp));
    assert(!temp); // 'temp' is now explicitly null
    
    db1 = sqlite_move(temp);
    assert(!db1); // db1 should now be null (its previous handle safely closed)
}

void test_database_hooks() {
    printf("7. Testing SqliteDatabaseView connection hooks and event handlers...\n");

    SqliteDatabaseOwned db(":memory:");
    assert(db);

    // Track hook events
    struct HookTracker {
        int update_count = 0;
        int last_op = 0;
        sqlite3_int64 last_rowid = 0;
        int commit_count = 0;
        int rollback_count = 0;
        int progress_count = 0;
    } tracker;

    // Register update hook
    db.set_update_hook([](void* user_data, int op, const char* db_name, const char* table_name, sqlite3_int64 rowid) {
        (void)db_name;
        (void)table_name;
        HookTracker* t = static_cast<HookTracker*>(user_data);
        t->update_count++;
        t->last_op = op;
        t->last_rowid = rowid;
    }, &tracker);

    // Register commit hook
    db.set_commit_hook([](void* user_data) -> int {
        HookTracker* t = static_cast<HookTracker*>(user_data);
        t->commit_count++;
        return 0; // 0 = allow commit
    }, &tracker);

    // Register rollback hook
    db.set_rollback_hook([](void* user_data) {
        HookTracker* t = static_cast<HookTracker*>(user_data);
        t->rollback_count++;
    }, &tracker);

    // Register progress handler
    db.set_progress_handler(1, [](void* user_data) -> int {
        HookTracker* t = static_cast<HookTracker*>(user_data);
        t->progress_count++;
        return 0; // 0 = continue
    }, &tracker);

    // Set busy timeout
    assert(db.busy_timeout(1000) == SQLITE_OK);

    // 1. DDL & Insert
    assert(db.exec("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT);") == SQLITE_OK);
    assert(db.exec("INSERT INTO items (id, name) VALUES (1, 'apple');") == SQLITE_OK);

    assert(db.last_insert_rowid() == 1);
    assert(db.changes() == 1);
    assert(tracker.update_count == 1);
    assert(tracker.last_op == SQLITE_INSERT);
    assert(tracker.last_rowid == 1);

    // 2. Update
    assert(db.exec("UPDATE items SET name = 'banana' WHERE id = 1;") == SQLITE_OK);
    assert(tracker.update_count == 2);
    assert(tracker.last_op == SQLITE_UPDATE);
    assert(tracker.last_rowid == 1);

    // 3. Delete
    assert(db.exec("DELETE FROM items WHERE id = 1;") == SQLITE_OK);
    assert(tracker.update_count == 3);
    assert(tracker.last_op == SQLITE_DELETE);

    // 4. Test Transaction Hooks
    {
        SqliteTransaction txn(db);
        assert(txn.exec("INSERT INTO items (id, name) VALUES (10, 'cherry');") == SQLITE_OK);
        assert(txn.commit() == SQLITE_OK);
    }
    assert(tracker.commit_count >= 1);

    {
        SqliteTransaction txn(db);
        assert(txn.exec("INSERT INTO items (id, name) VALUES (20, 'date');") == SQLITE_OK);
        assert(txn.rollback() == SQLITE_OK);
    }
    assert(tracker.rollback_count >= 1);

    // Verify progress handler was called during queries
    assert(tracker.progress_count > 0);

    // Total changes check
    assert(db.total_changes() >= 4);
}

// Global counters for compile-time template trampoline test
static int g_template_update_count = 0;
static int g_template_commit_count = 0;
static int g_template_rollback_count = 0;
static int g_template_progress_count = 0;

static void on_template_update(int op, const char* db, const char* tbl, sqlite3_int64 rowid) {
    (void)op; (void)db; (void)tbl; (void)rowid;
    g_template_update_count++;
}

static int on_template_commit() {
    g_template_commit_count++;
    return 0;
}

static void on_template_rollback() {
    g_template_rollback_count++;
}

static int on_template_progress() {
    g_template_progress_count++;
    return 0;
}

void test_database_template_hooks() {
    printf("8. Testing SqliteDatabaseView compile-time template hook trampolines...\n");

    SqliteDatabaseOwned db(":memory:");
    assert(db);

    // Register via zero-overhead compile-time function pointer templates!
    db.set_update_hook<on_template_update>();
    db.set_commit_hook<on_template_commit>();
    db.set_rollback_hook<on_template_rollback>();
    db.set_progress_handler<on_template_progress>(1);

    assert(db.exec("CREATE TABLE t_items (id INT); INSERT INTO t_items VALUES (1);") == SQLITE_OK);
    assert(g_template_update_count == 1);

    {
        SqliteTransaction txn(db);
        assert(txn.exec("INSERT INTO t_items VALUES (2);") == SQLITE_OK);
        assert(txn.commit() == SQLITE_OK);
    }
    assert(g_template_commit_count >= 1);

    {
        SqliteTransaction txn(db);
        assert(txn.exec("INSERT INTO t_items VALUES (3);") == SQLITE_OK);
        assert(txn.rollback() == SQLITE_OK);
    }
    assert(g_template_rollback_count >= 1);
    assert(g_template_progress_count > 0);
}

struct TypedAuditContext {
    int insert_count = 0;
    int update_count = 0;
    int delete_count = 0;
    int commit_count = 0;
    int rollback_count = 0;
    int progress_ticks = 0;
    int wal_pages = 0;
};

void test_database_strongly_typed_hooks() {
    printf("9. Testing SqliteDatabaseView strongly-typed UserData* hook templates...\n");

    SqliteDatabaseOwned db(":memory:");
    assert(db);

    TypedAuditContext ctx;

    // 1. Strongly typed update hook (no void* in callback signature!)
    db.set_update_hook<TypedAuditContext>([](TypedAuditContext* c, int op, const char* db_name, const char* table_name, sqlite3_int64 rowid) {
        (void)db_name; (void)table_name; (void)rowid;
        if (op == SQLITE_INSERT) c->insert_count++;
        else if (op == SQLITE_UPDATE) c->update_count++;
        else if (op == SQLITE_DELETE) c->delete_count++;
    }, &ctx);

    // 2. Strongly typed commit hook
    db.set_commit_hook<TypedAuditContext>([](TypedAuditContext* c) -> int {
        c->commit_count++;
        return 0;
    }, &ctx);

    // 3. Strongly typed rollback hook
    db.set_rollback_hook<TypedAuditContext>([](TypedAuditContext* c) {
        c->rollback_count++;
    }, &ctx);

    // 4. Strongly typed progress handler
    db.set_progress_handler<TypedAuditContext>(1, [](TypedAuditContext* c) -> int {
        c->progress_ticks++;
        return 0;
    }, &ctx);

    assert(db.exec("CREATE TABLE typed_tbl (id INT);") == SQLITE_OK);
    assert(db.exec("INSERT INTO typed_tbl VALUES (10);") == SQLITE_OK);
    assert(ctx.insert_count == 1);

    assert(db.exec("UPDATE typed_tbl SET id = 20 WHERE id = 10;") == SQLITE_OK);
    assert(ctx.update_count == 1);

    assert(db.exec("DELETE FROM typed_tbl WHERE id = 20;") == SQLITE_OK);
    assert(ctx.delete_count == 1);

    {
        SqliteTransaction txn(db);
        assert(txn.exec("INSERT INTO typed_tbl VALUES (30);") == SQLITE_OK);
        assert(txn.commit() == SQLITE_OK);
    }
    assert(ctx.commit_count >= 1);

    {
        SqliteTransaction txn(db);
        assert(txn.exec("INSERT INTO typed_tbl VALUES (40);") == SQLITE_OK);
        assert(txn.rollback() == SQLITE_OK);
    }
    assert(ctx.rollback_count >= 1);
    assert(ctx.progress_ticks > 0);
}

static int g_template_wal_pages = 0;
static int on_template_wal(sqlite3* db, const char* db_name, int num_pages) {
    (void)db; (void)db_name;
    g_template_wal_pages += num_pages;
    return SQLITE_OK;
}

void test_database_wal_and_abort_hooks() {
    printf("10. Testing SqliteDatabaseView WAL hook & hook abort/interrupt handlers...\n");

    const char* wal_db_file = "test_wal_hooks.db";
    remove(wal_db_file);

    // Test WAL Hook with disk database
    {
        SqliteDatabaseOwned db(wal_db_file);
        assert(db);
        assert(db.exec("PRAGMA journal_mode=WAL;") == SQLITE_OK);

        // Bind WAL hook via compile-time template
        db.set_wal_hook<on_template_wal>();

        assert(db.exec("CREATE TABLE wal_test (id INT);") == SQLITE_OK);
        assert(db.exec("INSERT INTO wal_test VALUES (1);") == SQLITE_OK);
        assert(g_template_wal_pages > 0);

        // Also test strongly-typed UserData WAL hook
        TypedAuditContext wal_ctx;
        db.set_wal_hook<TypedAuditContext>([](TypedAuditContext* c, sqlite3* d, const char* name, int pages) -> int {
            (void)d; (void)name;
            c->wal_pages += pages;
            return SQLITE_OK;
        }, &wal_ctx);

        assert(db.exec("INSERT INTO wal_test VALUES (2);") == SQLITE_OK);
        assert(wal_ctx.wal_pages > 0);
    }
    remove(wal_db_file);
    remove("test_wal_hooks.db-wal");
    remove("test_wal_hooks.db-shm");

    // Test Commit Hook Abort (returning non-zero cancels commit)
    {
        SqliteDatabaseOwned db(":memory:");
        db.exec("CREATE TABLE abort_test (id INT);");

        // Commit hook that always denies commit
        db.set_commit_hook([](void*) -> int {
            return 1; // Deny / Abort commit
        });

        SqliteTransaction txn(db);
        assert(txn.exec("INSERT INTO abort_test VALUES (999);") == SQLITE_OK);
        int rc = txn.commit();
        assert(rc != SQLITE_OK); // Must fail to commit!
    }

    // Test Progress Handler Interrupt
    {
        SqliteDatabaseOwned db(":memory:");
        db.exec("CREATE TABLE prog_test (id INT);");

        // Progress handler that immediately interrupts
        db.set_progress_handler(1, [](void*) -> int {
            return 1; // Interrupt query!
        });

        auto stmt = db.prepare("INSERT INTO prog_test VALUES (1);");
        int rc = stmt.step();
        assert(rc == SQLITE_INTERRUPT);
    }
}

void test_database_diagnostics() {
    printf("11. Testing SqliteDatabaseView diagnostics, autocommit, and error handlers...\n");

    SqliteDatabaseOwned db(":memory:");
    assert(db);

    // 1. autocommit status
    assert(db.is_autocommit() == true);
    {
        SqliteTransaction txn(db);
        assert(db.is_autocommit() == false); // Inside transaction -> autocommit is OFF
        assert(txn.commit() == SQLITE_OK);
    }
    assert(db.is_autocommit() == true);

    // 2. read-only check
    assert(db.is_readonly("main") == false);

    // 3. Error inspection
    int bad_rc = db.exec("SELECT * FROM non_existent_table;");
    assert(bad_rc != SQLITE_OK);
    assert(db.errcode() == SQLITE_ERROR);
    assert(db.extended_errcode() == SQLITE_ERROR);
    assert(db.errmsg() != nullptr);
    assert(SqliteDatabaseView::errstr(SQLITE_OK) != nullptr);

    // 4. wal_checkpoint
    int ckpt_rc = db.wal_checkpoint("main", SQLITE_CHECKPOINT_PASSIVE);
    assert(ckpt_rc == SQLITE_OK);
}

int main() {
    sqlite3_initialize();

    test_database_owned();
    test_database_view_transactions();
    test_database_exec();
    test_database_moves();
    test_database_exec_multiple();
    test_database_move_edge_cases();
    test_database_hooks();
    test_database_template_hooks();
    test_database_strongly_typed_hooks();
    test_database_wal_and_abort_hooks();
    test_database_diagnostics();

    sqlite3_shutdown();

    printf("\nAll 11 SqliteDatabase Test Suites Passed Successfully!\n");
    return 0;
}
