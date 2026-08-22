#define SQLITE_CORE
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "sqlite3_statement.hpp"

// Custom destructor tracking to test bind dtor callbacks
static int g_custom_dtor_calls = 0;
static void test_custom_dtor(void* ptr) {
    (void)ptr;
    g_custom_dtor_calls++;
}

void test_lifecycle_and_moves(sqlite3* db) {
    printf("1. Testing Lifecycle and Move Semantics...\n");

    // 1. Default constructor (unprepared)
    SqliteStatement empty_stmt;
    assert(!empty_stmt);
    assert(empty_stmt.get() == nullptr);
    assert(empty_stmt.step() == SQLITE_MISUSE);

    // 2. Prepare via constructor
    SqliteStatement stmt1(db, "SELECT 42;");
    assert(stmt1);
    assert(stmt1.get() != nullptr);

    // 3. Move constructor
    SqliteStatement stmt2(static_cast<SqliteStatement&&>(stmt1));
    assert(!stmt1);
    assert(stmt1.get() == nullptr);
    assert(stmt2);
    assert(stmt2.get() != nullptr);

    // 4. Step the moved-to statement
    assert(stmt2.step() == SQLITE_ROW);
    assert(stmt2.column_int(0) == 42);

    // 5. Move assignment while mid-query
    SqliteStatement stmt3(db, "SELECT 100;");
    stmt3 = static_cast<SqliteStatement&&>(stmt2);
    assert(!stmt2);
    assert(stmt3);
    assert(stmt3.column_int(0) == 42); // stmt3 inherited the active row

    // Reset and step again
    assert(stmt3.reset() == SQLITE_OK);
    assert(stmt3.step() == SQLITE_ROW);
    assert(stmt3.column_int(0) == 42);

    // 6. Self move-assignment check
    stmt3 = static_cast<SqliteStatement&&>(stmt3);
    assert(stmt3);
    assert(stmt3.column_int(0) == 42);

    // 7. Release handle
    sqlite3_stmt* raw = stmt3.release();
    assert(!stmt3);
    assert(raw != nullptr);
    sqlite3_finalize(raw);

    // 8. Re-prepare on finalized statement
    assert(stmt3.prepare(db, "SELECT 777;") == SQLITE_OK);
    assert(stmt3.step() == SQLITE_ROW);
    assert(stmt3.column_int(0) == 777);
    assert(stmt3.finalize() == SQLITE_OK);
    assert(!stmt3);
    assert(stmt3.finalize() == SQLITE_OK); // finalize on null returns SQLITE_OK
}

void test_execution_and_stepping(sqlite3* db) {
    printf("2. Testing Execution and Stepping...\n");

    // DDL Execution via execute()
    SqliteStatement create_stmt(db, "CREATE TABLE users(id INT PRIMARY KEY, name TEXT, score REAL, avatar BLOB);");
    assert(create_stmt.execute() == SQLITE_DONE);

    // Insert multiple rows
    SqliteStatement insert_stmt(db, "INSERT INTO users VALUES (?, ?, ?, ?);");
    
    // Row 1
    insert_stmt.bind(1, 1);
    insert_stmt.bind(2, "Alice");
    insert_stmt.bind(3, 95.5);
    unsigned char avatar1[] = {0x01, 0x02};
    insert_stmt.bind(4, avatar1, 2);
    assert(insert_stmt.execute() == SQLITE_DONE);

    // Row 2
    insert_stmt.bind(1, 2);
    insert_stmt.bind(2, "Bob");
    insert_stmt.bind(3, 88.0);
    insert_stmt.bind_null(4);
    assert(insert_stmt.execute() == SQLITE_DONE);

    // Row 3
    insert_stmt.bind(1, 3);
    insert_stmt.bind(2, "Charlie");
    insert_stmt.bind(3, 72.3);
    unsigned char avatar3[] = {0x0A, 0x0B, 0x0C};
    insert_stmt.bind(4, avatar3, 3);
    assert(insert_stmt.execute() == SQLITE_DONE);

    // Query rows using next() iteration
    SqliteStatement query(db, "SELECT id, name, score, avatar FROM users ORDER BY id ASC;");
    assert(query.column_count() == 4);
    assert(strcmp(query.column_name(0), "id") == 0);
    assert(strcmp(query.column_name(1), "name") == 0);
    assert(strcmp(query.column_name(2), "score") == 0);
    assert(strcmp(query.column_name(3), "avatar") == 0);

    int row_count = 0;
    while (query.next()) {
        row_count++;
        int id = query.column_int(0);
        SqliteStringView name = query.column_string_view(1);
        double score = query.column_double(2);

        if (id == 1) {
            assert(name == SqliteStringView("Alice"));
            assert(score == 95.5);
            SqliteBlobView avatar = query.column_blob_view(3);
            assert(avatar.size() == 2);
        } else if (id == 2) {
            assert(name == SqliteStringView("Bob"));
            assert(score == 88.0);
            assert(query.column_type(3) == SQLITE_NULL);
        } else if (id == 3) {
            assert(name == SqliteStringView("Charlie"));
            assert(score == 72.3);
            SqliteBlobView avatar = query.column_blob_view(3);
            assert(avatar.size() == 3);
        }
    }
    assert(row_count == 3);
}

void test_all_1based_bind_overloads(sqlite3* db) {
    printf("3. Testing All 1-Based Parameter Binding Overloads...\n");

    SqliteStatement stmt(db, "SELECT ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?;");
    
    // 1. int
    assert(stmt.bind(1, 10) == SQLITE_OK);
    // 2. sqlite3_int64
    assert(stmt.bind(2, static_cast<sqlite3_int64>(9000000000LL)) == SQLITE_OK);
    // 3. double
    assert(stmt.bind(3, 2.71828) == SQLITE_OK);
    // 4. bind_null
    assert(stmt.bind_null(4) == SQLITE_OK);
    // 5. const char* (with length and custom dtor)
    g_custom_dtor_calls = 0;
    char text_buf[] = "CustomDtorText";
    assert(stmt.bind(5, text_buf, 14, test_custom_dtor) == SQLITE_OK);
    // 6. SqliteStringView (with custom dtor)
    SqliteStringView sv("ViewString", 10);
    assert(stmt.bind(6, sv, test_custom_dtor) == SQLITE_OK);
    // 7. SqliteStringOwned
    SqliteStringOwned so("OwnedString");
    assert(stmt.bind(7, so) == SQLITE_OK);
    // 8. const void* blob (with custom dtor)
    unsigned char raw_blob[] = {0xAA, 0xBB};
    assert(stmt.bind(8, raw_blob, 2, test_custom_dtor) == SQLITE_OK);
    // 9. SqliteBlobView (with custom dtor)
    SqliteBlobView bv(raw_blob, 2);
    assert(stmt.bind(9, bv, test_custom_dtor) == SQLITE_OK);
    // 10. SqliteBlobOwned
    SqliteBlobOwned bo(raw_blob, 2);
    assert(stmt.bind(10, bo) == SQLITE_OK);
    // 11. SqliteValueView
    SqliteStatement temp_stmt(db, "SELECT 42;");
    assert(temp_stmt.step() == SQLITE_ROW);
    SqliteValueView vv = temp_stmt.column_value_view(0);
    assert(stmt.bind(11, vv) == SQLITE_OK);
    // 12. SqliteValueOwned
    SqliteValueOwned vo(3.14);
    assert(stmt.bind(12, vo) == SQLITE_OK);

    assert(stmt.step() == SQLITE_ROW);

    // Verify all 12 columns
    assert(stmt.column_int(0) == 10);
    assert(stmt.column_int64(1) == 9000000000LL);
    assert(stmt.column_double(2) == 2.71828);
    assert(stmt.column_type(3) == SQLITE_NULL);
    assert(strcmp(stmt.column_text(4), "CustomDtorText") == 0);
    assert(stmt.column_string_view(5) == SqliteStringView("ViewString"));
    assert(stmt.column_string_view(6) == SqliteStringView("OwnedString"));
    assert(stmt.column_bytes(7) == 2);
    assert(stmt.column_blob_view(8).size() == 2);
    assert(stmt.column_blob_view(9).size() == 2);
    assert(stmt.column_int(10) == 42);
    assert(stmt.column_double(11) == 3.14);
}

void test_all_column_extraction_functions(sqlite3* db) {
    printf("4. Testing All Column Extraction Methods...\n");

    SqliteStatement stmt(db, "SELECT 101 AS col_int, 9000000000 AS col_int64, 3.14159 AS col_double, 'sample_text' AS col_text, x'DEADBEEF' AS col_blob, NULL AS col_null;");
    assert(stmt.step() == SQLITE_ROW);

    // column_count
    assert(stmt.column_count() == 6);

    // column_name
    assert(strcmp(stmt.column_name(0), "col_int") == 0);
    assert(strcmp(stmt.column_name(1), "col_int64") == 0);
    assert(strcmp(stmt.column_name(2), "col_double") == 0);
    assert(strcmp(stmt.column_name(3), "col_text") == 0);
    assert(strcmp(stmt.column_name(4), "col_blob") == 0);
    assert(strcmp(stmt.column_name(5), "col_null") == 0);

    // column_type
    assert(stmt.column_type(0) == SQLITE_INTEGER);
    assert(stmt.column_type(1) == SQLITE_INTEGER);
    assert(stmt.column_type(2) == SQLITE_FLOAT);
    assert(stmt.column_type(3) == SQLITE_TEXT);
    assert(stmt.column_type(4) == SQLITE_BLOB);
    assert(stmt.column_type(5) == SQLITE_NULL);

    // column_int / column_int64 / column_double
    assert(stmt.column_int(0) == 101);
    assert(stmt.column_int64(1) == 9000000000LL);
    assert(stmt.column_double(2) == 3.14159);

    // column_text / column_bytes / column_string_view
    assert(strcmp(stmt.column_text(3), "sample_text") == 0);
    assert(stmt.column_bytes(3) == 11);
    SqliteStringView sv = stmt.column_string_view(3);
    assert(sv == SqliteStringView("sample_text", 11));

    // column_blob / column_blob_view
    const unsigned char expected_blob[] = {0xDE, 0xAD, 0xBE, 0xEF};
    assert(stmt.column_bytes(4) == 4);
    assert(memcmp(stmt.column_blob(4), expected_blob, 4) == 0);
    SqliteBlobView bv = stmt.column_blob_view(4);
    assert(bv.size() == 4);
    assert(memcmp(bv.data(), expected_blob, 4) == 0);

    // column_value / column_value_view / column_value_owned
    sqlite3_value* raw_val = stmt.column_value(0);
    assert(raw_val != nullptr);

    SqliteValueView vv = stmt.column_value_view(0);
    assert(vv.type() == SQLITE_INTEGER);
    assert(vv == 101);

    SqliteValueOwned vo = stmt.column_value_owned(3);
    assert(vo.type() == SQLITE_TEXT);
    assert(vo == SqliteStringView("sample_text"));
}

void test_named_parameter_binding(sqlite3* db) {
    printf("5. Testing Named Parameter Binding...\n");

    SqliteStatement stmt(db, "SELECT :msg, @score, $id, :raw_blob, :null_val, :str_view, :blob_view;");
    
    // bind_parameter_index
    assert(stmt.bind_parameter_index(":msg") == 1);
    assert(stmt.bind_parameter_index("@score") == 2);
    assert(stmt.bind_parameter_index("$id") == 3);
    assert(stmt.bind_parameter_index(":raw_blob") == 4);
    assert(stmt.bind_parameter_index(":null_val") == 5);
    assert(stmt.bind_parameter_index(":str_view") == 6);
    assert(stmt.bind_parameter_index(":blob_view") == 7);
    assert(stmt.bind_parameter_index(":missing") == 0);

    // Named template bindings
    assert(stmt.bind(":msg", "Named Param Works") == SQLITE_OK);
    assert(stmt.bind("@score", 99.9) == SQLITE_OK);
    assert(stmt.bind("$id", static_cast<sqlite3_int64>(123456789012LL)) == SQLITE_OK);
    
    unsigned char blob_data[] = {0xAA, 0xBB};
    assert(stmt.bind(":raw_blob", blob_data, 2) == SQLITE_OK);
    assert(stmt.bind_null(":null_val") == SQLITE_OK);

    SqliteStringView sv("NamedStrView", 12);
    assert(stmt.bind(":str_view", sv) == SQLITE_OK);

    SqliteBlobView bv(blob_data, 2);
    assert(stmt.bind(":blob_view", bv) == SQLITE_OK);

    // Missing parameter name returns SQLITE_NOTFOUND
    assert(stmt.bind(":missing", 1) == SQLITE_NOTFOUND);
    assert(stmt.bind(":missing", "text") == SQLITE_NOTFOUND);
    assert(stmt.bind(":missing", blob_data, 2) == SQLITE_NOTFOUND);
    assert(stmt.bind_null(":missing") == SQLITE_NOTFOUND);

    assert(stmt.step() == SQLITE_ROW);
    assert(stmt.column_string_view(0) == SqliteStringView("Named Param Works"));
    assert(stmt.column_double(1) == 99.9);
    assert(stmt.column_int64(2) == 123456789012LL);
    assert(stmt.column_blob_view(3).size() == 2);
    assert(stmt.column_type(4) == SQLITE_NULL);
    assert(stmt.column_string_view(5) == SqliteStringView("NamedStrView"));
    assert(stmt.column_blob_view(6).size() == 2);
}

void test_clear_bindings_and_statement_reuse(sqlite3* db) {
    printf("6. Testing clear_bindings() and statement reuse...\n");

    SqliteStatement stmt(db, "SELECT ?, ?;");
    stmt.bind(1, 100);
    stmt.bind(2, "Test");

    assert(stmt.step() == SQLITE_ROW);
    assert(stmt.column_int(0) == 100);
    assert(stmt.column_string_view(1) == SqliteStringView("Test"));

    // Reset and clear bindings
    assert(stmt.reset() == SQLITE_OK);
    assert(stmt.clear_bindings() == SQLITE_OK);

    // Stepping without re-binding should yield NULL for all parameters
    assert(stmt.step() == SQLITE_ROW);
    assert(stmt.column_type(0) == SQLITE_NULL);
    assert(stmt.column_type(1) == SQLITE_NULL);

    // Re-bind new values
    assert(stmt.reset() == SQLITE_OK);
    stmt.bind(1, 200);
    stmt.bind(2, "Updated");
    assert(stmt.step() == SQLITE_ROW);
    assert(stmt.column_int(0) == 200);
    assert(stmt.column_string_view(1) == SqliteStringView("Updated"));
}

void test_explicit_nbyte_prepare(sqlite3* db) {
    printf("7. Testing explicit nByte prepare substring...\n");

    const char* multi_sql = "SELECT 111; SELECT 222;";
    // Prepare only the first 11 bytes: "SELECT 111;"
    SqliteStatement stmt(db, multi_sql, 11);
    assert(stmt);
    assert(stmt.step() == SQLITE_ROW);
    assert(stmt.column_int(0) == 111);
}

void test_transactions_and_blob_owned(sqlite3* db) {
    printf("8. Testing Transactions and SqliteBlobOwned binding...\n");

    SqliteStatement begin_tx(db, "BEGIN TRANSACTION;");
    assert(begin_tx.execute() == SQLITE_DONE);

    SqliteStatement insert_tx(db, "INSERT INTO users VALUES (?, ?, ?, ?);");
    insert_tx.bind(1, 10);
    insert_tx.bind(2, "TxUser");
    insert_tx.bind(3, 100.0);
    
    unsigned char raw_blob[] = {0x11, 0x22, 0x33, 0x44};
    SqliteBlobOwned owned_blob(raw_blob, 4);
    insert_tx.bind(4, owned_blob);
    assert(insert_tx.execute() == SQLITE_DONE);

    SqliteStatement commit_tx(db, "COMMIT;");
    assert(commit_tx.execute() == SQLITE_DONE);

    // Query to verify
    SqliteStatement query(db, "SELECT avatar FROM users WHERE id = 10;");
    assert(query.next());
    SqliteBlobView bv = query.column_blob_view(0);
    assert(bv.size() == 4);
    assert(memcmp(bv.data(), raw_blob, 4) == 0);
}

void test_error_handling(sqlite3* db) {
    printf("9. Testing Error Handling & Unprepared Defaults...\n");

    // Syntax error in prepare
    SqliteStatement bad_stmt(db, "SELECT FROM WHERE INVALID;");
    assert(!bad_stmt);

    // Prepare with nullptr
    SqliteStatement null_stmt;
    assert(null_stmt.prepare(nullptr, "SELECT 1;") == SQLITE_MISUSE);
    assert(null_stmt.prepare(db, nullptr) == SQLITE_MISUSE);

    // Invalid operations on unprepared statement
    assert(null_stmt.bind(1, 42) == SQLITE_MISUSE);
    assert(null_stmt.bind(1, static_cast<sqlite3_int64>(42)) == SQLITE_MISUSE);
    assert(null_stmt.bind(1, 3.14) == SQLITE_MISUSE);
    assert(null_stmt.bind(1, "text") == SQLITE_MISUSE);
    assert(null_stmt.bind(1, static_cast<const char*>(nullptr), 0) == SQLITE_MISUSE);
    assert(null_stmt.bind(1, static_cast<const void*>(nullptr), 0) == SQLITE_MISUSE);
    SqliteStringView sv("test", 4);
    assert(null_stmt.bind(1, sv) == SQLITE_MISUSE);
    SqliteStringOwned so("test");
    assert(null_stmt.bind(1, so) == SQLITE_MISUSE);
    SqliteBlobView bv("blob", 4);
    assert(null_stmt.bind(1, bv) == SQLITE_MISUSE);
    SqliteBlobOwned bo("blob", 4);
    assert(null_stmt.bind(1, bo) == SQLITE_MISUSE);
    SqliteValueOwned vo(123);
    assert(null_stmt.bind(1, vo) == SQLITE_MISUSE);
    SqliteValueView vv(nullptr);
    assert(null_stmt.bind(1, vv) == SQLITE_MISUSE);

    assert(null_stmt.bind_null(1) == SQLITE_MISUSE);
    assert(null_stmt.bind_parameter_index(":test") == 0);
    assert(null_stmt.step() == SQLITE_MISUSE);
    assert(!null_stmt.next());
    assert(null_stmt.reset() == SQLITE_MISUSE);
    assert(null_stmt.clear_bindings() == SQLITE_MISUSE);
    assert(null_stmt.execute() == SQLITE_MISUSE);

    // Column queries on empty statement return safe defaults
    assert(null_stmt.column_count() == 0);
    assert(null_stmt.column_type(0) == SQLITE_NULL);
    assert(null_stmt.column_name(0) == nullptr);
    assert(null_stmt.column_int(0) == 0);
    assert(null_stmt.column_int64(0) == 0);
    assert(null_stmt.column_double(0) == 0.0);
    assert(null_stmt.column_text(0) == nullptr);
    assert(null_stmt.column_bytes(0) == 0);
    assert(null_stmt.column_blob(0) == nullptr);
    assert(null_stmt.column_value(0) == nullptr);
    assert(null_stmt.column_string_view(0).data() == nullptr);
    assert(null_stmt.column_blob_view(0).data() == nullptr);
}

int main() {
    sqlite3_initialize();

    sqlite3* db;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
        printf("Failed to open memory db\n");
        return 1;
    }

    test_lifecycle_and_moves(db);
    test_execution_and_stepping(db);
    test_all_1based_bind_overloads(db);
    test_all_column_extraction_functions(db);
    test_named_parameter_binding(db);
    test_clear_bindings_and_statement_reuse(db);
    test_explicit_nbyte_prepare(db);
    test_transactions_and_blob_owned(db);
    test_error_handling(db);

    sqlite3_close(db);
    sqlite3_shutdown();

    printf("\nAll 9 SqliteStatement Test Suites (100%% Function Coverage) Passed Successfully!\n");
    return 0;
}
