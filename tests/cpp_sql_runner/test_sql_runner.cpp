#define SQLITE_CORE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite3_sql_runner.hpp"
#include "sqlite3_db.hpp"

using namespace sqlite_ext;

// ============================================================================
// 1. SqlTableBuffer Lifecycle, Reallocation, and SBO Growth
// ============================================================================
void test_table_buffer_lifecycle() {
    printf("1. Testing SqlTableBuffer lifecycle, growth, and move semantics...\n");

    SqlTableBuffer buf;
    assert(buf.count == 0);
    assert(buf.capacity == 0);
    assert(!buf.is_present);
    assert(!buf.skip_validation);

    // Initial reset on empty buffer
    buf.reset();
    assert(buf.count == 0);

    // Test add_row when capacity is 0 -> allocates 8
    SqliteValueVec<8> row1(1);
    row1[0] = SqliteValueOwned(static_cast<sqlite3_int64>(42));
    buf.add_row(sqlite_move(row1));
    assert(buf.count == 1);
    assert(buf.capacity == 8);

    // Add 30 more rows with 15 columns each (tests growth 8 -> 16 -> 32, and SBO heap spill past 8 cols)
    for (int r = 1; r < 31; ++r) {
        SqliteValueVec<8> r_vec(15);
        for (int c = 0; c < 15; ++c) {
            r_vec[c] = SqliteValueOwned(static_cast<sqlite3_int64>(r * 100 + c));
        }
        buf.add_row(sqlite_move(r_vec));
    }

    assert(buf.count == 31);
    assert(buf.capacity >= 31);
    assert(buf.rows[0].size() == 1);
    assert(buf.rows[30].size() == 15);
    assert(buf.rows[30][14].as_int64() == 3014);

    // Move Constructor
    SqlTableBuffer buf2(sqlite_move(buf));
    assert(buf.count == 0);
    assert(buf.rows == nullptr);
    assert(buf2.count == 31);
    assert(buf2.rows[30][14].as_int64() == 3014);

    // Move Assignment into empty buffer
    SqlTableBuffer buf3;
    buf3 = sqlite_move(buf2);
    assert(buf2.count == 0);
    assert(buf2.rows == nullptr);
    assert(buf3.count == 31);
    assert(buf3.rows[0][0].as_int64() == 42);

    // Move Assignment into already-populated buffer (tests safe reset of target)
    SqlTableBuffer buf4;
    SqliteValueVec<8> temp_row(2);
    temp_row[0] = SqliteValueOwned(static_cast<sqlite3_int64>(1));
    temp_row[1] = SqliteValueOwned(static_cast<sqlite3_int64>(2));
    buf4.add_row(sqlite_move(temp_row));
    assert(buf4.count == 1);

    buf4 = sqlite_move(buf3);
    assert(buf3.count == 0);
    assert(buf4.count == 31);

    // Self move assignment
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
    buf4 = sqlite_move(buf4);
#pragma GCC diagnostic pop
    assert(buf4.count == 31);

    buf4.reset();
    assert(buf4.count == 0);
    assert(buf4.rows == nullptr);
    printf("   [PASS] SqlTableBuffer lifecycle verified.\n");
}

// ============================================================================
// 2. Unit Tests for String, Parser, and Formatting Helpers
// ============================================================================
void test_sql_runner_helpers() {
    printf("2. Testing SQL Runner string helpers, formatters, and table dividers...\n");

    // 1. trim_whitespace tests
    assert(SqliteSqlRunner::trim_whitespace(nullptr) == nullptr);

    char empty_str[] = "";
    assert(strcmp(SqliteSqlRunner::trim_whitespace(empty_str), "") == 0);

    char all_spaces[] = "   \t\r\n   ";
    assert(strcmp(SqliteSqlRunner::trim_whitespace(all_spaces), "") == 0);

    char leading_trailing[] = "  \t Hello World \r\n ";
    assert(strcmp(SqliteSqlRunner::trim_whitespace(leading_trailing), "Hello World") == 0);

    char no_spaces[] = "ExactWord";
    assert(strcmp(SqliteSqlRunner::trim_whitespace(no_spaces), "ExactWord") == 0);

    // 2. is_table_divider tests
    assert(!SqliteSqlRunner::is_table_divider(""));
    assert(!SqliteSqlRunner::is_table_divider("col1 | col2"));
    assert(!SqliteSqlRunner::is_table_divider("| col1 | col2 |"));
    assert(SqliteSqlRunner::is_table_divider("|---|---|"));
    assert(SqliteSqlRunner::is_table_divider("|:---|:---:|---:|"));
    assert(SqliteSqlRunner::is_table_divider("+---+---+"));
    assert(SqliteSqlRunner::is_table_divider("  | --- | --- |  "));
    assert(SqliteSqlRunner::is_table_divider("  :--- | :---  "));
    assert(SqliteSqlRunner::is_table_divider("  --- | ---  "));
    assert(!SqliteSqlRunner::is_table_divider("| --- | bad_char |"));

    // 3. parse_cell_value tests
    assert(SqliteSqlRunner::parse_cell_value(nullptr).is_null());
    assert(SqliteSqlRunner::parse_cell_value("NULL").is_null());
    assert(SqliteSqlRunner::parse_cell_value("null").is_null());

    SqliteValueOwned vi1 = SqliteSqlRunner::parse_cell_value("0");
    assert(vi1.is_integer() && vi1.as_int64() == 0);

    SqliteValueOwned vi2 = SqliteSqlRunner::parse_cell_value("9223372036854775807");
    assert(vi2.is_integer() && vi2.as_int64() == 9223372036854775807LL);

    SqliteValueOwned vi3 = SqliteSqlRunner::parse_cell_value("-123456");
    assert(vi3.is_integer() && vi3.as_int64() == -123456);

    SqliteValueOwned vf1 = SqliteSqlRunner::parse_cell_value("3.1415926");
    assert(vf1.is_float() && vf1.as_double() == 3.1415926);

    SqliteValueOwned vf2 = SqliteSqlRunner::parse_cell_value("-0.005");
    assert(vf2.is_float() && vf2.as_double() == -0.005);

    SqliteValueOwned vt1 = SqliteSqlRunner::parse_cell_value("Hello World");
    assert(vt1.is_text() && vt1.as_text() == "Hello World");

    SqliteValueOwned vt2 = SqliteSqlRunner::parse_cell_value("123abc");
    assert(vt2.is_text() && vt2.as_text() == "123abc");

    SqliteValueOwned vt3 = SqliteSqlRunner::parse_cell_value("");
    assert(vt3.is_null());

    SqliteValueOwned vt4 = SqliteSqlRunner::parse_cell_value("''");
    assert(vt4.is_text() && vt4.as_text().empty());

    SqliteValueOwned vb1 = SqliteSqlRunner::parse_cell_value("true");
    assert(vb1.is_bool() && vb1.as_bool());

    SqliteValueOwned vb2 = SqliteSqlRunner::parse_cell_value("false");
    assert(vb2.is_bool() && !vb2.as_bool());

    SqliteValueOwned v_blob = SqliteSqlRunner::parse_cell_value("X'DEADBEEF'");
    assert(v_blob.is_blob() && v_blob.as_blob().size() == 4);

    SqliteValueOwned v_sci = SqliteSqlRunner::parse_cell_value("1.5e-3");
    assert(v_sci.is_float());

    SqliteValueOwned v_plus = SqliteSqlRunner::parse_cell_value("+42");
    assert(v_plus.is_integer() && v_plus.as_int64() == 42);

    // 4. format_value tests
    char buf[128];
    SqliteValueOwned val_null;
    SqliteSqlRunner::format_value(val_null, buf, sizeof(buf));
    assert(strcmp(buf, "NULL") == 0);

    SqliteValueOwned val_int(static_cast<sqlite3_int64>(123456789LL));
    SqliteSqlRunner::format_value(val_int, buf, sizeof(buf));
    assert(strcmp(buf, "123456789") == 0);

    SqliteValueOwned val_float(2.71828);
    SqliteSqlRunner::format_value(val_float, buf, sizeof(buf));
    assert(strncmp(buf, "2.7183", 6) == 0);

    SqliteValueOwned val_text("Test string");
    SqliteSqlRunner::format_value(val_text, buf, sizeof(buf));
    assert(strcmp(buf, "Test string") == 0);

    SqliteValueOwned val_blob = SqliteValueOwned::from_blob("BINARY_DATA", 11);
    SqliteSqlRunner::format_value(val_blob, buf, sizeof(buf));
    assert(strstr(buf, "(BLOB 11 bytes)") != nullptr);

    // 5. print_separator
    SqliteSqlRunner::print_separator(3, 10);

    printf("   [PASS] SQL Runner string helpers, formatters, and table dividers verified.\n");
}

// ============================================================================
// 3. Unit Tests for parse_snapshot_block Directive Parsing
// ============================================================================
void test_snapshot_block_parsing() {
    printf("3. Testing parse_snapshot_block directive permutations...\n");

    SqlTableBuffer snap;

    // 1. Null / empty text
    SqliteSqlRunner::parse_snapshot_block(nullptr, snap);
    assert(!snap.is_present);
    SqliteSqlRunner::parse_snapshot_block("", snap);
    assert(!snap.is_present);

    // 2. Skip directives
    SqliteSqlRunner::parse_snapshot_block("-- @snapshot: skip\n", snap);
    assert(snap.is_present && snap.skip_validation);

    SqliteSqlRunner::parse_snapshot_block("-- @snapshot:skip\n", snap);
    assert(snap.is_present && snap.skip_validation);

    SqliteSqlRunner::parse_snapshot_block("  \t -- @snapshot: skip \r\n", snap);
    assert(snap.is_present && snap.skip_validation);

    // 3. No snapshot present
    SqliteSqlRunner::parse_snapshot_block("SELECT 1;", snap);
    assert(!snap.is_present);

    // 4. Intervening SQL before snapshot (invalidates snapshot association)
    SqliteSqlRunner::parse_snapshot_block("SELECT 42;\n-- @snapshot\n-- | 42 |\n", snap);
    assert(!snap.is_present);

    // 5. Intervening comments before snapshot (valid)
    const char* snap_with_comments =
        "-- Some preparatory comment\n"
        "-- Another comment line\n"
        "-- @snapshot\n"
        "-- | col1 | col2 |\n"
        "-- |---|---|\n"
        "-- | 10 | Alpha |\n";
    SqliteSqlRunner::parse_snapshot_block(snap_with_comments, snap);
    assert(snap.is_present && !snap.skip_validation);
    assert(snap.count == 1);
    assert(snap.rows[0][0].as_int64() == 10);
    assert(snap.rows[0][1].as_text() == "Alpha");

    // 6. Compact prefix `--@snapshot` and `--|`
    const char* compact_snap =
        "--@snapshot\n"
        "--| a | b |\n"
        "--|---|---|\n"
        "--| 1 | 2 |\n";
    SqliteSqlRunner::parse_snapshot_block(compact_snap, snap);
    assert(snap.is_present && !snap.skip_validation);
    assert(snap.count == 1);
    assert(snap.rows[0][0].as_int64() == 1);
    assert(snap.rows[0][1].as_int64() == 2);

    // 7. Non-comment line terminating snapshot block
    const char* snap_multi_rows =
        "-- @snapshot\n"
        "-- | id | name | price |\n"
        "-- |:---|:---|:---|\n"
        "-- | 1  | Item1 | 10.5  |\n"
        "-- | 2  | Item2 | 20.0  |\n"
        "-- | 3  | Item3 | NULL  |\n"
        "SELECT * FROM next_table;\n";
    SqliteSqlRunner::parse_snapshot_block(snap_multi_rows, snap);
    assert(snap.is_present && snap.count == 3);
    assert(snap.rows[0][2].as_double() == 10.5);
    assert(snap.rows[1][2].as_double() == 20.0);
    assert(snap.rows[2][2].is_null());

    printf("   [PASS] parse_snapshot_block directive permutations verified.\n");
}

// ============================================================================
// 4. Execution, Cells, Multi-Statements, and Assertion Paths
// ============================================================================
void test_sql_runner_execution_coverage() {
    printf("4. Testing SqliteSqlRunner::run_string execution branches & edge cases...\n");

    SqliteDatabaseOwned db = SqliteDatabaseOwned::open_memory();
    assert(db.is_open());
    assert(db.is_valid());
    assert(static_cast<bool>(db));

    // 1. Null / Empty arguments
    assert(!SqliteSqlRunner::run_string(nullptr, "SELECT 1;"));
    assert(!SqliteSqlRunner::run_string(db, nullptr));
    assert(SqliteSqlRunner::run_string(db, ""));

    // 2. Cell without title prefix (default fallback title)
    const char* kNoTitleScript =
        "CREATE TABLE t1(x INT);\n"
        "INSERT INTO t1 VALUES(42);\n"
        "SELECT x FROM t1;\n"
        "-- @snapshot\n"
        "-- | x |\n"
        "-- |--|\n"
        "-- | 42 |\n";
    assert(SqliteSqlRunner::run_string(db, kNoTitleScript, "No Title Cell"));

    // 3. Multi-cell, with `-- %% Title` and compact `--%%Title`
    const char* kMultiCellScript =
        "-- %% 01. Create & Insert\n"
        "CREATE TABLE accounts(id INT PRIMARY KEY, owner TEXT, balance REAL);\n"
        "INSERT INTO accounts VALUES(1, 'Alice', 1000.50);\n"
        "INSERT INTO accounts VALUES(2, 'Bob', 250.00);\n"
        "\n"
        "--%%02. Compact Cell Header\n"
        "SELECT id, owner, balance FROM accounts ORDER BY id;\n"
        "-- @snapshot\n"
        "-- | id | owner | balance |\n"
        "-- |---|---|---|\n"
        "-- | 1  | Alice | 1000.5  |\n"
        "-- | 2  | Bob   | 250.0   |\n"
        "\n"
        "-- %% 03. Empty Result Assertion\n"
        "SELECT id, owner FROM accounts WHERE id = 999;\n"
        "-- @snapshot\n"
        "-- | id | owner |\n"
        "-- |---|---|\n"
        "\n"
        "-- %% 04. Skip Directive\n"
        "SELECT random() AS rnd;\n"
        "-- @snapshot: skip\n"
        "\n"
        "-- %% 05. Optional Snapshots Mode (require_snapshots = false)\n"
        "SELECT COUNT(*) AS total FROM accounts;\n";

    assert(SqliteSqlRunner::run_string(db, kMultiCellScript, "Multi-Cell Batch", false));

    // 4. Wide table with > 8 columns (testing SBO spill in live execution and snapshot)
    const char* kWideTableScript =
        "-- %% Wide Table 10 Columns\n"
        "CREATE TABLE wide(c1 INT, c2 INT, c3 INT, c4 INT, c5 INT, c6 INT, c7 INT, c8 INT, c9 INT, c10 INT);\n"
        "INSERT INTO wide VALUES(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);\n"
        "SELECT * FROM wide;\n"
        "-- @snapshot\n"
        "-- | c1 | c2 | c3 | c4 | c5 | c6 | c7 | c8 | c9 | c10 |\n"
        "-- |---|---|---|---|---|---|---|---|---|---|\n"
        "-- | 1  | 2  | 3  | 4  | 5  | 6  | 7  | 8  | 9  | 10  |\n";
    assert(SqliteSqlRunner::run_string(db, kWideTableScript, "Wide Table Test"));

    // 5. UTF-8 international text support
    const char* kUtf8Script =
        "-- %% UTF-8 International Strings\n"
        "CREATE TABLE i18n(lang TEXT, greeting TEXT);\n"
        "INSERT INTO i18n VALUES('JP', 'こんにちは'), ('FR', 'Bonjour');\n"
        "SELECT lang, greeting FROM i18n ORDER BY lang;\n"
        "-- @snapshot\n"
        "-- | lang | greeting |\n"
        "-- |---|---|\n"
        "-- | FR   | Bonjour |\n"
        "-- | JP   | こんにちは |\n";
    assert(SqliteSqlRunner::run_string(db, kUtf8Script, "UTF-8 Test"));

    // 6. Comments only in cell / empty comments
    const char* kCommentsOnly =
        "-- %% Comments Only\n"
        "-- Line 1\n"
        "-- Line 2\n";
    assert(SqliteSqlRunner::run_string(db, kCommentsOnly, "Comments Only Test"));

    // 7. Error: Missing mandatory snapshot
    const char* kMissingSnap =
        "-- %% Missing Snapshot\n"
        "SELECT * FROM accounts;\n";
    assert(!SqliteSqlRunner::run_string(db, kMissingSnap, "Missing Snapshot Fail", true));

    // 8. Error: Snapshot row count mismatch
    const char* kRowCountMismatch =
        "-- %% Row Count Mismatch\n"
        "SELECT id FROM accounts;\n"
        "-- @snapshot\n"
        "-- | id |\n"
        "-- |--|\n"
        "-- | 1  |\n"; // expected 1, actual is 2
    assert(!SqliteSqlRunner::run_string(db, kRowCountMismatch, "Row Count Fail", true));

    // 9. Error: Snapshot cell value mismatch
    const char* kCellValueMismatch =
        "-- %% Cell Value Mismatch\n"
        "SELECT id, owner FROM accounts WHERE id = 1;\n"
        "-- @snapshot\n"
        "-- | id | owner |\n"
        "-- |--|--|\n"
        "-- | 1  | WrongName |\n";
    assert(!SqliteSqlRunner::run_string(db, kCellValueMismatch, "Cell Value Fail", true));

    // 10. Error: SQL syntax prepare error
    const char* kSyntaxError =
        "-- %% Syntax Error\n"
        "SELECT * FROM NON_EXISTENT_TABLE_XYZ WHERE;\n";
    assert(!SqliteSqlRunner::run_string(db, kSyntaxError, "Syntax Error Fail", false));

    // 11. Error: DML runtime execution error (PRIMARY KEY constraint violation)
    const char* kConstraintViolation =
        "-- %% Constraint Error\n"
        "INSERT INTO accounts(id, owner, balance) VALUES(1, 'Duplicate ID', 500.0);\n";
    assert(!SqliteSqlRunner::run_string(db, kConstraintViolation, "Constraint Fail", false));

    printf("   [PASS] SqliteSqlRunner::run_string execution branches & edge cases verified.\n");
}

// ============================================================================
// 5. File Execution & Filepath Fallback Resolution Tests
// ============================================================================
void test_sql_runner_file_execution() {
    printf("5. Testing SqliteSqlRunner::run_file with valid, empty, and non-existent files...\n");

    SqliteDatabaseOwned db = SqliteDatabaseOwned::open_memory();

    // 1. Null parameters
    assert(!SqliteSqlRunner::run_file(nullptr, "dummy.sql"));
    assert(!SqliteSqlRunner::run_file(db, nullptr));

    // 2. Non-existent file
    assert(!SqliteSqlRunner::run_file(db, "definitely_not_existing_file_99999.sql"));

    // 3. Empty file (size <= 0)
    const char* empty_filename = "test_empty_script_temp.sql";
    FILE* fe = fopen(empty_filename, "wb");
    assert(fe != nullptr);
    fclose(fe);
    assert(SqliteSqlRunner::run_file(db, empty_filename));
    remove(empty_filename);

    // 4. Valid file execution
    const char* filename = "test_valid_script_temp.sql";
    FILE* f = fopen(filename, "wb");
    assert(f != nullptr);

    const char* kFileContent =
        "-- %% File Execution Test\n"
        "CREATE TABLE products(sku TEXT PRIMARY KEY, qty INT);\n"
        "INSERT INTO products VALUES('A100', 50), ('B200', 100);\n"
        "SELECT sku, qty FROM products ORDER BY sku;\n"
        "-- @snapshot\n"
        "-- | sku  | qty |\n"
        "-- |---|---|\n"
        "-- | A100 | 50  |\n"
        "-- | B200 | 100 |\n";

    fwrite(kFileContent, 1, strlen(kFileContent), f);
    fclose(f);

    bool ok = SqliteSqlRunner::run_file(db, filename);
    assert(ok);

    // 5. Test convenience free functions and macros with fresh in-memory databases
    {
        SqliteDatabaseOwned db2 = SqliteDatabaseOwned::open_memory();
        assert(sqlite_run_file(db2, filename));
    }
    {
        SqliteDatabaseOwned db3 = SqliteDatabaseOwned::open_memory();
        assert(SQLITE_RUN_SQL_FILE(db3, filename));
    }
    {
        SqliteDatabaseOwned db4 = SqliteDatabaseOwned::open_memory();
        assert(SQLITE_RUN_SQL_FILE_EX(db4, filename, true));
    }

    // 6. Test run_file_with_init
    bool ok_init = SqliteSqlRunner::run_file_with_init(filename, [](sqlite3* d) {
        assert(d != nullptr);
        return SQLITE_OK;
    });
    assert(ok_init);

    // 7. Test run_string_with_init
    bool ok_str_init = SqliteSqlRunner::run_string_with_init(kFileContent, "Init Title", [](sqlite3* d) {
        assert(d != nullptr);
        return SQLITE_OK;
    });
    assert(ok_str_init);

    // 8. Test initialization error propagation
    bool fail_init = SqliteSqlRunner::run_file_with_init(filename, [](sqlite3*) {
        return SQLITE_ERROR;
    });
    assert(!fail_init);

    remove(filename);

    printf("   [PASS] SqliteSqlRunner::run_file verified.\n");
}

// ============================================================================
// Main Test Entry Point
// ============================================================================
int main() {
    printf("=================================================================\n");
    printf("Running 100%% Coverage SQL Script Runner (sqlite3_sql_runner.hpp) Tests\n");
    printf("=================================================================\n");

    test_table_buffer_lifecycle();
    test_sql_runner_helpers();
    test_snapshot_block_parsing();
    test_sql_runner_execution_coverage();
    test_sql_runner_file_execution();

    printf("=================================================================\n");
    printf("All SQL Script Runner Test Suites Passed Successfully (100%%)!\n");
    printf("=================================================================\n");
    return 0;
}
