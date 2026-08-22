#define SQLITE_CORE
#include <sqlite3.h>
#include <stdio.h>
#include <cassert>
#include <cstring>
#include "sqlite3_udf.hpp"
#include "sqlite3_tvf.hpp"
#include "sqlite3_statement.hpp"

// ============================================================================
// TVF 1: generate_series (Integer Range)
// ============================================================================
struct SeriesIterator : public SqliteTvfIterator {
    static constexpr const char* schema() {
        // 'value' is the output column. 
        // 'start', 'stop', 'step' are hidden columns acting as input parameters.
        return "CREATE TABLE x(value, start hidden, stop hidden, step hidden)";
    }

    sqlite3_int64 m_current;
    sqlite3_int64 m_stop;
    sqlite3_int64 m_step;

    void init(SqliteUdfArgs args) override {
        // args[0] = start, args[1] = stop, args[2] = step
        m_current = args.size() > 0 && args[0].type() != SQLITE_NULL ? args[0].as_int64() : 0;
        m_stop    = args.size() > 1 && args[1].type() != SQLITE_NULL ? args[1].as_int64() : 0;
        m_step    = args.size() > 2 && args[2].type() != SQLITE_NULL ? args[2].as_int64() : 1;
        
        // Prevent infinite loops
        if (m_step == 0) m_step = 1; 
    }

    void next() override {
        m_current += m_step;
    }

    bool eof() const override {
        if (m_step > 0) return m_current > m_stop;
        return m_current < m_stop;
    }

    void column(sqlite3_context* ctx, int col_idx) override {
        if (col_idx == 0) {
            sqlite3_result_int64(ctx, m_current);
        }
    }

    sqlite3_int64 rowid() const override {
        return m_current;
    }
};

void run_tests() {
    sqlite3* db;
    assert(sqlite3_open(":memory:", &db) == SQLITE_OK);

    // Register the TVF
    assert(SqliteUdf::define_tvf<SeriesIterator>(db, "my_series") == SQLITE_OK);

    // Test 1: Forward Series (1 to 5)
    {
        SqliteStatement stmt(db, "SELECT value FROM my_series(1, 5);");
        sqlite3_int64 expected = 1;
        while (stmt.next()) {
            assert(stmt.column_int64(0) == expected);
            expected++;
        }
        assert(expected == 6);
    }

    // Test 2: Backward Series (5 to 1, step -1)
    {
        SqliteStatement stmt(db, "SELECT value FROM my_series(5, 1, -1);");
        sqlite3_int64 expected = 5;
        while (stmt.next()) {
            assert(stmt.column_int64(0) == expected);
            expected--;
        }
        assert(expected == 0);
    }

    // Test 3: JOIN with TVF
    {
        assert(sqlite3_exec(db, "CREATE TABLE inputs(target INT);", nullptr, nullptr, nullptr) == SQLITE_OK);
        assert(sqlite3_exec(db, "INSERT INTO inputs VALUES (2), (4);", nullptr, nullptr, nullptr) == SQLITE_OK);
        
        // This query tests if SQLite correctly passes the varying t.target down to the TVF's xFilter!
        SqliteStatement stmt(db, "SELECT t.target, s.value FROM inputs t JOIN my_series(1, t.target) s;");
        
        // Expected pairs: (2,1), (2,2), (4,1), (4,2), (4,3), (4,4)
        int count = 0;
        while (stmt.next()) {
            count++;
        }
        assert(count == 6);
    }

    // Test 4: Filtering on the output column natively
    {
        // This tests that if we query `WHERE value % 2 == 0`, SQLite's native filtering
        // correctly absorbs it without our TVF choking on `value` as an input constraint.
        SqliteStatement stmt(db, "SELECT value FROM my_series(1, 10) WHERE value % 2 = 0;");
        
        // Expected pairs: 2, 4, 6, 8, 10
        int expected = 2;
        int count = 0;
        while (stmt.next()) {
            assert(stmt.column_int64(0) == expected);
            expected += 2;
            count++;
        }
        assert(count == 5);
    }

    // Test 5: Multiple TVF instances (Cartesian Product / CROSS JOIN)
    {
        // Tests that multiple instances of the same TVF can run simultaneously 
        // without their state clashing.
        SqliteStatement stmt(db, "SELECT a.value, b.value FROM my_series(1, 2) a, my_series(3, 4) b;");
        
        int count = 0;
        while (stmt.next()) {
            count++;
        }
        // Expected: (1,3), (1,4), (2,3), (2,4) -> 4 rows
        assert(count == 4);
    }

    // Test 6: Missing arguments
    {
        // Tests that SQLite passes NULL for missing arguments and our init() handles it safely.
        // my_series(1) provides start=1, stop=NULL, step=NULL.
        // Our init() defaults stop=0, step=1. 1 to 0 with step 1 produces 0 rows.
        SqliteStatement stmt(db, "SELECT value FROM my_series(1);");
        
        int count = 0;
        while (stmt.next()) {
            count++;
        }
        assert(count == 0);
    }

    // Test 7: Zero arguments
    {
        // Tests passing absolutely no arguments to the TVF.
        // It defaults to start=0, stop=0, step=1.
        // 0 to 0 with step 1 yields exactly one row (0).
        SqliteStatement stmt(db, "SELECT value FROM my_series();");
        
        assert(stmt.step() == SQLITE_ROW);
        assert(stmt.column_int(0) == 0);
        assert(stmt.step() == SQLITE_DONE);
    }

    // Test 8: Double-Correlated Subquery
    {
        // Tests joining a table where MULTIPLE hidden arguments are drawn from the outer loop!
        assert(sqlite3_exec(db, "CREATE TABLE ranges(start INT, stop INT);", nullptr, nullptr, nullptr) == SQLITE_OK);
        assert(sqlite3_exec(db, "INSERT INTO ranges VALUES (1, 3), (10, 11);", nullptr, nullptr, nullptr) == SQLITE_OK);
        
        SqliteStatement stmt(db, "SELECT t.start, t.stop, s.value FROM ranges t JOIN my_series(t.start, t.stop) s;");
        
        // Expected pairs: 
        // Range 1 (1 to 3): 1, 2, 3
        // Range 2 (10 to 11): 10, 11
        // Total rows: 5
        int count = 0;
        while (stmt.next()) {
            count++;
        }
        assert(count == 5);
    }

    // Test 9: Invalid Data Types
    {
        // SQLite's loose typing means strings will be gracefully converted to 0 by our `as_int64()`
        // 'a' -> 0, 'b' -> 0, 'c' -> 0 (step coerced to 1). Yields 1 row (0).
        SqliteStatement stmt(db, "SELECT value FROM my_series('a', 'b', 'c');");
        
        assert(stmt.step() == SQLITE_ROW);
        assert(stmt.column_int(0) == 0);
        assert(stmt.step() == SQLITE_DONE);
    }

    // Test 10: High-Volume Eponymous Generation
    {
        // Tests that our O(1) memory iterator can generate a massive dataset instantly
        // without running into recursion or memory limits.
        SqliteStatement stmt(db, "SELECT count(*) FROM my_series(1, 1000000);");
        
        assert(stmt.step() == SQLITE_ROW);
        assert(stmt.column_int(0) == 1000000); // exactly 1,000,000 rows generated dynamically!
    }

    sqlite3_close(db);
    printf("All TVF tests passed cleanly!\n");
}

int main() {
    run_tests();
    return 0;
}
