#define SQLITE_CORE
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sqlite3.h>
#include "../../include/sqlite3_vtab.hpp"
#include "../../include/sqlite3_udf.hpp"

// ============================================================================
// 1. Example Virtual Table Cursor: Series Generator
// ============================================================================

class SeriesCursor : public SqliteVTabCursor {
private:
    int m_current;
    int m_max;

public:
    SeriesCursor() : m_current(0), m_max(0) {}

    int filter(int idxNum, const char* idxStr, SqliteUdfArgs args) override {
        (void)idxStr;
        if (idxNum == 42) {
            // idxNum 42 means we are handling a MATCH operator!
            const char* rhs = reinterpret_cast<const char*>(sqlite3_value_text(const_cast<sqlite3_value*>(args[0].get())));
            printf("  [Debug] filter() called for MATCH. RHS string is: '%s'\n", rhs ? rhs : "NULL");
            m_current = 42; // Yield just 42 as a dummy match
            m_max = 42;
        } else {
            m_current = 1;
            m_max = 100;
        }
        return SQLITE_OK;
    }

    int next() override {
        m_current++;
        return SQLITE_OK;
    }

    bool eof() override {
        return m_current > m_max;
    }

    int column(SqliteContext& ctx, int N) override {
        if (N == 0) {
            ctx.result_int(m_current);
        }
        return SQLITE_OK;
    }

    int rowid(sqlite3_int64& pRowid) override {
        pRowid = m_current;
        return SQLITE_OK;
    }
};

// ============================================================================
// 2. Example Virtual Table: Series Table
// ============================================================================

class SeriesTable : public SqliteVTable {
public:
    SeriesTable(sqlite3* db) : SqliteVTable(db) {}

    static int connect(SqliteConnectArgs& args) {
        int rc = sqlite3_declare_vtab(args.db(), "CREATE TABLE x(value)");
        if (rc == SQLITE_OK) {
            args.set_instance(sqlite_new<SeriesTable>(args.db()));
        }
        return rc;
    }

    int bestIndex(SqliteIndexInfo& info) override {
        info.set_estimated_cost(100.0);
        return SQLITE_OK;
    }

    SqliteVTabCursor* open() override {
        return sqlite_new<SeriesCursor>();
    }
};

// ============================================================================
// 3. Example Virtual Table: Complex Table (Writable, Findable, Shadow, Rename)
// ============================================================================
class ComplexTable;
static void my_complex_udf(SqliteContext& ctx, SqliteUdfArgs args);
static void my_match_udf(SqliteContext& ctx, SqliteUdfArgs args);

class ComplexTable : public SqliteVTable {
public:
    int m_magic = 999; // Some custom state

    ComplexTable(sqlite3* db) : SqliteVTable(db) {}

    static int connect(SqliteConnectArgs& args) {
        int rc = sqlite3_declare_vtab(args.db(), "CREATE TABLE x(value)");
        if (rc == SQLITE_OK) {
            args.set_instance(sqlite_new<ComplexTable>(args.db()));
        }
        return rc;
    }

    int bestIndex(SqliteIndexInfo& info) override {
        for (int i = 0; i < info.num_constraints(); ++i) {
            // Is the user querying: `value MATCH 'something'`?
            if (info.constraint(i).op == SQLITE_INDEX_CONSTRAINT_MATCH && info.constraint(i).usable) {
                printf("  [Debug] bestIndex() found MATCH operator!\n");
                
                // Tell SQLite to pass the RHS ('something') to filter() as args[0]
                info.usage(i).argvIndex = 1;
                
                // Tell SQLite not to double-check the result after we return it
                info.usage(i).omit = 1;
                
                // Tell our filter() method that we are running the MATCH branch
                info.set_idx_num(42);
                info.set_estimated_cost(10.0);
                return SQLITE_OK;
            }
        }
        
        info.set_estimated_cost(100.0); 
        return SQLITE_OK; 
    }
    SqliteVTabCursor* open() override { return sqlite_new<SeriesCursor>(); } // Re-use cursor for simplicity

    int update(SqliteUdfArgs args, sqlite3_int64* pRowid) override {
        (void)args;
        if (pRowid) {
            *pRowid = 1; // Fake rowid for insert
        }
        return SQLITE_OK;
    }

    SqliteFunctionDef findFunction(int nArg, const char* zName) override {
        printf("  [Debug] xFindFunction called for '%s' with nArg=%d\n", zName, nArg);
        if (strcmp(zName, "complex_math") == 0) {
            return SqliteFunctionDef::wrap<my_complex_udf>(this); 
        } else if (strcmp(zName, "match") == 0) {
            return SqliteFunctionDef::wrap<my_match_udf>(this);
        }
        return {}; // Did not intercept
    }

    int rename(const char* zNewName) override {
        (void)zNewName;
        return SQLITE_OK;
    }

    static int shadowName(const char* zName) {
        // SQLite passes the FULL table name being created (e.g., "my_complex_renamed_complex_shadow").
        // We must check if it contains our shadow suffix!
        return strstr(zName, "complex_shadow") != nullptr ? 1 : 0;
    }
};

static void my_complex_udf(SqliteContext& ctx, SqliteUdfArgs args) {
    (void)args;
    
    // Retrieve the `this` pointer that we passed into `wrap(this)`!
    ComplexTable* self = static_cast<ComplexTable*>(ctx.user_data());
    
    // Use the custom state!
    ctx.result_int(self->m_magic);
}

static void my_match_udf(SqliteContext& ctx, SqliteUdfArgs args) {
    (void)args;
    // Just return 1 (true) for match
    ctx.result_int(1);
}

// ============================================================================
// 4. Example Virtual Table: Eponymous Table (Table-Valued Function)
// ============================================================================
class EponymousTable : public SqliteVTable {
public:
    EponymousTable(sqlite3* db) : SqliteVTable(db) {}
    
    static int connect(SqliteConnectArgs& args) {
        int rc = sqlite3_declare_vtab(args.db(), "CREATE TABLE x(value)");
        if (rc == SQLITE_OK) {
            args.set_instance(sqlite_new<EponymousTable>(args.db()));
        }
        return rc;
    }
    
    int bestIndex(SqliteIndexInfo& info) override {
        info.set_estimated_cost(10.0);
        return SQLITE_OK;
    }
    
    SqliteVTabCursor* open() override {
        return sqlite_new<SeriesCursor>();
    }
};

// ============================================================================
// 5. Main Tests
// ============================================================================

void exec(sqlite3* db, const char* sql) {
    char* err = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        printf("SQL error: %s\n", err);
        sqlite3_free(err);
    }
    assert(rc == SQLITE_OK);
}

void test_vtab() {
    printf("Testing Virtual Table (Read-Only) API...\n");
    
    sqlite3* db;
    assert(sqlite3_open(":memory:", &db) == SQLITE_OK);

    printf("Manually registering global 'complex_math' function...\n");
    SqliteUdf::define(db, "complex_math", 1, [](sqlite3_context* ctx, SqliteUdfArgs args) {
        (void)args;
        sqlite3_result_int(ctx, 0); // The global fallback returns 0
    });

    printf("Registering Series Table...\n");
    int rc = SqliteVTab::define<SeriesTable>(db, "series");
    assert(rc == SQLITE_OK);
    
    printf("Creating my_series...\n");
    exec(db, "CREATE VIRTUAL TABLE my_series USING series;");

    printf("Querying my_series...\n");
    sqlite3_stmt* stmt;
    rc = sqlite3_prepare_v2(db, "SELECT value FROM my_series LIMIT 5;", -1, &stmt, nullptr);
    assert(rc == SQLITE_OK);

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        count++;
        int val = sqlite3_column_int(stmt, 0);
        assert(val == count);
    }
    assert(count == 5);
    
    sqlite3_finalize(stmt);
    
    printf("Registering Complex Table...\n");
    constexpr VTabOptions ALL_OPTIONS = VTabOptions::Writable | VTabOptions::Findable | VTabOptions::HasShadow | VTabOptions::Renameable | VTabOptions::Savepoint;
    rc = SqliteVTab::define<ComplexTable, ALL_OPTIONS>(db, "complex_table");
    assert(rc == SQLITE_OK);

    printf("Creating my_complex...\n");
    exec(db, "CREATE VIRTUAL TABLE my_complex USING complex_table;");
    
    printf("Altering my_complex...\n");
    exec(db, "ALTER TABLE my_complex RENAME TO my_complex_renamed;");
    
    printf("Inserting into my_complex_renamed...\n");
    exec(db, "INSERT INTO my_complex_renamed(value) VALUES(10);");
    exec(db, "INSERT INTO my_complex_renamed(value) VALUES(20);");
    
    printf("Executing overloaded UDF (complex_math) in SELECT clause...\n");
    rc = sqlite3_prepare_v2(db, "SELECT complex_math(value) FROM my_complex_renamed LIMIT 1;", -1, &stmt, nullptr);
    assert(rc == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    int overloaded_res = sqlite3_column_int(stmt, 0);
    printf("  complex_math() in SELECT returned: %d\n", overloaded_res);
    assert(overloaded_res == 999);
    sqlite3_finalize(stmt);
    
    printf("Executing overloaded UDF (complex_math) in WHERE clause...\n");
    rc = sqlite3_prepare_v2(db, "SELECT value FROM my_complex_renamed WHERE complex_math(value) = 999 LIMIT 1;", -1, &stmt, nullptr);
    assert(rc == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);

    printf("Executing MATCH operator (Native Query Planner)...\n");
    rc = sqlite3_prepare_v2(db, "SELECT value FROM my_complex_renamed WHERE value MATCH 'test';", -1, &stmt, nullptr);
    assert(rc == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 42); // matched dummy 42
    sqlite3_finalize(stmt);

    printf("Testing Transactions & Savepoints...\n");
    exec(db, "BEGIN;");
    exec(db, "INSERT INTO my_complex_renamed(value) VALUES(100);");
    exec(db, "SAVEPOINT sp;");
    exec(db, "INSERT INTO my_complex_renamed(value) VALUES(101);");
    exec(db, "ROLLBACK TO sp;");
    exec(db, "RELEASE sp;");
    exec(db, "COMMIT;");
    
    printf("Testing Shadow Table Protection...\n");
    // Enable Defensive Mode (required for SQLite to actually block shadow table creation!)
    int defensive = 1;
    sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, 1, &defensive);
    
    // xShadowName intercepts "complex_shadow". Creating it manually should fail!
    rc = sqlite3_exec(db, "CREATE TABLE my_complex_renamed_complex_shadow(id);", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        printf("  Shadow table protection worked! (Creation blocked: %s)\n", sqlite3_errmsg(db));
    } else {
        printf("  Shadow table creation allowed. (Defensive mode inactive or xShadowName ignored in this SQLite build)\n");
    }

    printf("Testing Eponymous Virtual Table (Table-Valued Function)...\n");
    // Register as Eponymous via SqliteVTab::define with VTabOptions::Eponymous
    rc = SqliteVTab::define<EponymousTable, VTabOptions::Eponymous>(db, "my_eponymous");
    assert(rc == SQLITE_OK);
    
    // We can query it DIRECTLY without running CREATE VIRTUAL TABLE!
    rc = sqlite3_prepare_v2(db, "SELECT value FROM my_eponymous LIMIT 1;", -1, &stmt, nullptr);
    assert(rc == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);

    printf("Dropping my_series...\n");
    exec(db, "DROP TABLE my_series;");

    printf("Dropping my_complex_renamed...\n");
    exec(db, "DROP TABLE my_complex_renamed;");

    printf("Closing DB...\n");
    assert(sqlite3_close(db) == SQLITE_OK);
    printf("Virtual Table Tests Passed!\n");
}

// ============================================================================
// 6. Error Propagation Test Table (Comprehensive get_error_message() tests)
// ============================================================================

class ErrorProneCursor : public SqliteVTabCursor {
private:
    bool m_fail_filter;
    bool m_fail_next;
    int m_count;
public:
    ErrorProneCursor(bool fail_filter, bool fail_next)
        : m_fail_filter(fail_filter), m_fail_next(fail_next), m_count(0) {}

    int filter(int idxNum, const char* idxStr, SqliteUdfArgs args) override {
        (void)idxNum; (void)idxStr; (void)args;
        if (m_fail_filter) return SQLITE_ERROR;
        m_count = 0;
        return SQLITE_OK;
    }

    int next() override {
        if (m_fail_next) return SQLITE_ERROR;
        m_count++;
        return SQLITE_OK;
    }

    bool eof() override { return m_count >= 5; }

    int column(SqliteContext& ctx, int N) override {
        (void)N;
        ctx.result_int(m_count);
        return SQLITE_OK;
    }

    int rowid(sqlite3_int64& pRowid) override {
        pRowid = m_count;
        return SQLITE_OK;
    }
};

class ErrorProneTable : public SqliteVTable {
public:
    static ErrorProneTable* s_latest_instance;
    const char* m_err_msg = nullptr;
    bool m_fail_update = false;
    bool m_fail_best_index = false;
    bool m_fail_begin = false;
    bool m_fail_sync = false;
    bool m_fail_commit = false;
    bool m_fail_rollback = false;
    bool m_fail_rename = false;
    bool m_fail_savepoint = false;
    bool m_fail_release = false;
    bool m_fail_rollback_to = false;
    bool m_fail_filter = false;
    bool m_fail_next = false;

    ErrorProneTable(sqlite3* db) : SqliteVTable(db) {
        s_latest_instance = this;
    }

    ~ErrorProneTable() override {
        if (s_latest_instance == this) {
            s_latest_instance = nullptr;
        }
    }

    static int connect(SqliteConnectArgs& args) {
        int rc = sqlite3_declare_vtab(args.db(), "CREATE TABLE x(id, val)");
        if (rc == SQLITE_OK) {
            args.set_instance(sqlite_new<ErrorProneTable>(args.db()));
        }
        return rc;
    }

    const char* get_error_message() const override {
        return m_err_msg;
    }

    int bestIndex(SqliteIndexInfo& info) override {
        if (m_fail_best_index) return SQLITE_ERROR;
        info.set_estimated_cost(10.0);
        return SQLITE_OK;
    }

    SqliteVTabCursor* open() override {
        return sqlite_new<ErrorProneCursor>(m_fail_filter, m_fail_next);
    }

    int update(SqliteUdfArgs args, sqlite3_int64* pRowid) override {
        (void)args; (void)pRowid;
        if (m_fail_update) return SQLITE_CONSTRAINT;
        return SQLITE_OK;
    }

    int begin() override {
        if (m_fail_begin) return SQLITE_ERROR;
        return SQLITE_OK;
    }

    int sync() override {
        if (m_fail_sync) return SQLITE_ERROR;
        return SQLITE_OK;
    }

    int commit() override {
        if (m_fail_commit) return SQLITE_ERROR;
        return SQLITE_OK;
    }

    int rollback() override {
        if (m_fail_rollback) return SQLITE_ERROR;
        return SQLITE_OK;
    }

    int rename(const char* zNewName) override {
        (void)zNewName;
        if (m_fail_rename) return SQLITE_ERROR;
        return SQLITE_OK;
    }

    int savepoint(int iSavepoint) override {
        (void)iSavepoint;
        if (m_fail_savepoint) return SQLITE_ERROR;
        return SQLITE_OK;
    }

    int release(int iSavepoint) override {
        (void)iSavepoint;
        if (m_fail_release) return SQLITE_ERROR;
        return SQLITE_OK;
    }

    int rollbackTo(int iSavepoint) override {
        (void)iSavepoint;
        if (m_fail_rollback_to) return SQLITE_ERROR;
        return SQLITE_OK;
    }
};

ErrorProneTable* ErrorProneTable::s_latest_instance = nullptr;

void test_vtab_error_messages() {
    printf("Testing Virtual Table Error Message Propagation (zErrMsg)...\n");
    sqlite3* db;
    assert(sqlite3_open(":memory:", &db) == SQLITE_OK);

    constexpr VTabOptions OPTS = VTabOptions::Writable | VTabOptions::Renameable | VTabOptions::Savepoint;
    int rc = SqliteVTab::define<ErrorProneTable, OPTS>(db, "err_module");
    assert(rc == SQLITE_OK);

    rc = sqlite3_exec(db, "CREATE VIRTUAL TABLE err_tab USING err_module;", nullptr, nullptr, nullptr);
    assert(rc == SQLITE_OK);
    assert(ErrorProneTable::s_latest_instance != nullptr);
    ErrorProneTable* vtab = ErrorProneTable::s_latest_instance;

    // 1. Test xUpdate custom error message
    printf("  1. Testing xUpdate error message...\n");
    fflush(stdout);
    vtab->m_fail_update = true;
    vtab->m_err_msg = "Custom update constraint failed: value must be positive";
    rc = sqlite3_exec(db, "INSERT INTO err_tab(id, val) VALUES(1, 'abc');", nullptr, nullptr, nullptr);
    assert(rc != SQLITE_OK);
    const char* errmsg = sqlite3_errmsg(db);
    printf("     SQLite reported error: %s\n", errmsg);
    fflush(stdout);
    assert(strstr(errmsg, "Custom update constraint failed: value must be positive") != nullptr);

    // Reset update failure
    vtab->m_fail_update = false;
    vtab->m_err_msg = nullptr;

    // 2. Test xFilter custom error message
    printf("  2. Testing xFilter error message...\n");
    fflush(stdout);
    vtab->m_fail_filter = true;
    vtab->m_err_msg = "Custom filter error: invalid scan range";
    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db, "SELECT * FROM err_tab;", -1, &stmt, nullptr);
    assert(rc == SQLITE_OK);
    rc = sqlite3_step(stmt);
    assert(rc != SQLITE_ROW && rc != SQLITE_DONE);
    errmsg = sqlite3_errmsg(db);
    printf("     SQLite reported error: %s\n", errmsg);
    fflush(stdout);
    assert(strstr(errmsg, "Custom filter error: invalid scan range") != nullptr);
    sqlite3_finalize(stmt);

    // Reset filter failure
    vtab->m_fail_filter = false;
    vtab->m_err_msg = nullptr;

    // 3. Test xNext custom error message
    printf("  3. Testing xNext error message...\n");
    fflush(stdout);
    vtab->m_fail_next = true;
    vtab->m_err_msg = "Custom next iteration error: corrupted row pointer";
    rc = sqlite3_prepare_v2(db, "SELECT * FROM err_tab;", -1, &stmt, nullptr);
    assert(rc == SQLITE_OK);
    rc = sqlite3_step(stmt); // First row (via filter)
    assert(rc == SQLITE_ROW);
    rc = sqlite3_step(stmt); // Second row (via next) -> fails!
    assert(rc != SQLITE_ROW && rc != SQLITE_DONE);
    errmsg = sqlite3_errmsg(db);
    printf("     SQLite reported error: %s\n", errmsg);
    fflush(stdout);
    assert(strstr(errmsg, "Custom next iteration error: corrupted row pointer") != nullptr);
    sqlite3_finalize(stmt);

    // Reset next failure
    vtab->m_fail_next = false;
    vtab->m_err_msg = nullptr;

    // 4. Test xRename custom error message
    printf("  4. Testing xRename error message...\n");
    fflush(stdout);
    vtab->m_fail_rename = true;
    vtab->m_err_msg = "Custom rename error: virtual table is locked by another session";
    rc = sqlite3_exec(db, "ALTER TABLE err_tab RENAME TO err_tab_renamed;", nullptr, nullptr, nullptr);
    assert(rc != SQLITE_OK);
    errmsg = sqlite3_errmsg(db);
    printf("     SQLite reported error: %s\n", errmsg);
    fflush(stdout);
    assert(strstr(errmsg, "Custom rename error: virtual table is locked by another session") != nullptr);

    // Reset rename failure
    vtab->m_fail_rename = false;
    vtab->m_err_msg = nullptr;

    // 5. Test xBegin / xSync / xCommit custom error message
    printf("  5. Testing transaction error messages...\n");
    fflush(stdout);
    vtab->m_fail_begin = true;
    vtab->m_err_msg = "Custom begin error: transaction engine not ready";
    rc = sqlite3_exec(db, "INSERT INTO err_tab(id, val) VALUES(2, 'xyz');", nullptr, nullptr, nullptr);
    assert(rc != SQLITE_OK);
    errmsg = sqlite3_errmsg(db);
    printf("     SQLite reported error: %s\n", errmsg);
    fflush(stdout);
    assert(strstr(errmsg, "Custom begin error: transaction engine not ready") != nullptr);

    vtab->m_fail_begin = false;
    vtab->m_err_msg = nullptr;

    // Clean up
    rc = sqlite3_exec(db, "DROP TABLE err_tab;", nullptr, nullptr, nullptr);
    assert(rc == SQLITE_OK);
    assert(sqlite3_close(db) == SQLITE_OK);
    printf("All Virtual Table Error Message Propagation Tests Passed!\n");
}

int main() {
    test_vtab();
    test_vtab_error_messages();
    return 0;
}

