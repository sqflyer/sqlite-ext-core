#define SQLITE_CORE
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sqlite3.h>
#include "sqlite3_statement.hpp"
#include "sqlite3_row.hpp"
#include "sqlite3_db.hpp"

// ============================================================================
// 1. Test SqliteRowView over Prepared Statement
// ============================================================================
void test_row_view_statement(sqlite3* db) {
    printf("Testing SqliteRowView over SqliteStatement...\n");

    assert(sqlite3_exec(db, "CREATE TABLE users(id INT, name TEXT, score REAL, avatar BLOB, extra JSON);", nullptr, nullptr, nullptr) == SQLITE_OK);
    assert(sqlite3_exec(db, "INSERT INTO users VALUES(101, 'Alice', 98.5, X'DEADBEEF', '{\"role\":\"admin\"}');", nullptr, nullptr, nullptr) == SQLITE_OK);

    SqliteStatement stmt(db, "SELECT id, name, score, avatar, extra FROM users WHERE id = 101;");
    assert(stmt.step() == SQLITE_ROW);

    // 1. Direct row() extraction
    SqliteRowView row = stmt.row();
    assert(row.size() == 5);
    assert(row.column_count() == 5);
    assert(!row.empty());

    // 2. Column names & decltypes
    assert(strcmp(row.column_name(0), "id") == 0);
    assert(strcmp(row.column_name(1), "name") == 0);
    assert(strcmp(row.column_name(2), "score") == 0);
    assert(strcmp(row.column_name(3), "avatar") == 0);
    assert(strcmp(row.column_name(4), "extra") == 0);

    // 3. Typed accessors
    assert(row.as_int64(0) == 101);
    assert(row[0].is_integer());
    assert(row.as_text(1) == "Alice");
    assert(row[1].is_text());
    assert(row.as_double(2) == 98.5);
    assert(row[2].is_float());
    assert(row.as_blob(3).size() == 4);
    assert(row[3].is_blob());
    assert(row.as_text(4) == "{\"role\":\"admin\"}");

    // 4. Out of bounds safety
    assert(row[10].is_null());
    assert(row[-1].is_null());
    assert(row.column_name(10) == nullptr);

    // 5. Range-based for loop iteration
    int col_idx = 0;
    for (SqliteValueView val : row) {
        assert(val.is_valid());
        if (col_idx == 0) assert(val.as_int64() == 101);
        if (col_idx == 1) assert(val.as_text() == "Alice");
        if (col_idx == 2) assert(val.as_double() == 98.5);
        if (col_idx == 3) assert(val.as_blob().size() == 4);
        if (col_idx == 4) assert(val.as_text() == "{\"role\":\"admin\"}");
        col_idx++;
    }
    assert(col_idx == 5);

    // 6. Materialize snapshot to owned dynamic row
    SqliteRowDynamic snapshot = row.to_owned();
    assert(snapshot.size() == 5);
    assert(snapshot.as_int64(0) == 101);
    assert(snapshot.as_text(1) == "Alice");
}

// ============================================================================
// 2. Test SqliteRowView over SqliteUdfArgs
// ============================================================================
void test_row_view_udf_args() {
    printf("Testing SqliteRowView over SqliteUdfArgs & SqliteValueViewArray...\n");

    SqliteValueOwned v1(42LL);
    SqliteValueOwned v2 = SqliteValueOwned::from_text("hello");
    SqliteValueOwned v3(3.14);

    // Create a contiguous owned array and view it
    SqliteValueOwned args_arr[3] = { sqlite_move(v1), sqlite_move(v2), sqlite_move(v3) };
    SqliteRowView row(args_arr, 3);

    assert(row.size() == 3);
    assert(row.as_int64(0) == 42);
    assert(row.as_text(1) == "hello");
    assert(row.as_double(2) == 3.14);

    // Test SqliteValueViewArray construction and extractions
    sqlite3_value* raw_vals[2] = { nullptr, nullptr };
    SqliteValueViewArray val_array(2, raw_vals);
    assert(val_array.size() == 2);
    assert(val_array.count() == 2);
    assert(!val_array.empty());
    assert(val_array[0].is_null());
    assert(val_array.as_int64(0) == 0);
    assert(val_array.as_double(0) == 0.0);
    assert(val_array.as_text(0).length() == 0);
    assert(val_array.as_blob(0).size() == 0);
    assert(!val_array.as_bool(0));
    assert(val_array[10].is_null()); // out of bounds
}

// ============================================================================
// 3. Test SqliteRowStatic (Stack-Allocated, 0 Mallocs)
// ============================================================================
void test_row_static_stack_allocation(sqlite3* db) {
    printf("Testing SqliteRowStatic stack allocation...\n");

    // Exact footprint check: 3 columns * 16 bytes = 48 bytes!
    static_assert(sizeof(SqliteRowStatic<3>) == 48, "SqliteRowStatic<3> must be exactly 48 bytes!");
    static_assert(sizeof(SqliteRowStatic<4>) == 64, "SqliteRowStatic<4> must be exactly 64 bytes!");

    SqliteRowStatic<3> stack_row;
    assert(stack_row.size() == 3);
    assert(stack_row[0].is_null());

    stack_row[0] = 202LL;
    stack_row[1] = SqliteValueOwned::from_text("Bob");
    stack_row[2] = 88.0;

    assert(stack_row[0].as_int64() == 202);
    assert(stack_row[1].as_text() == "Bob");
    assert(stack_row[2].as_double() == 88.0);

    // Bind columns into an INSERT statement
    assert(sqlite3_exec(db, "CREATE TABLE staff(id INT, name TEXT, rating REAL);", nullptr, nullptr, nullptr) == SQLITE_OK);

    SqliteStatement insert_stmt(db, "INSERT INTO staff VALUES(?, ?, ?);");
    assert(insert_stmt.bind(1, stack_row[0]) == SQLITE_OK);
    assert(insert_stmt.bind(2, stack_row[1]) == SQLITE_OK);
    assert(insert_stmt.bind(3, stack_row[2]) == SQLITE_OK);
    assert(insert_stmt.step() == SQLITE_DONE);

    // Verify row was inserted correctly
    SqliteStatement query_stmt(db, "SELECT id, name, rating FROM staff WHERE id = 202;");
    assert(query_stmt.step() == SQLITE_ROW);
    SqliteRowView q_row = query_stmt.row();
    assert(q_row.as_int64(0) == 202);
    assert(q_row.as_text(1) == "Bob");
    assert(q_row.as_double(2) == 88.0);

    // Construct static row from view
    SqliteRowStatic<3> copied_from_view(q_row);
    assert(copied_from_view.size() == 3);
    assert(copied_from_view[0].as_int64() == 202);
    assert(copied_from_view[1].as_text() == "Bob");
    assert(copied_from_view[2].as_double() == 88.0);
}

// ============================================================================
// 4. Test SqliteRowDynamic (Heap Allocation & Deep Move Semantics)
// ============================================================================
void test_row_dynamic_heap_and_moves() {
    printf("Testing SqliteRowDynamic heap allocation and move semantics...\n");

    SqliteRowDynamic dyn_row(4);
    assert(dyn_row.size() == 4);
    assert(dyn_row.column_count() == 4);
    assert(!dyn_row.empty());

    dyn_row[0] = 1001LL;
    dyn_row[1] = SqliteValueOwned::from_text("Charlie");
    dyn_row[2] = 3.14159;
    dyn_row[3] = SqliteValueOwned::from_blob("BLOB_DATA", 9);

    assert(dyn_row.as_int64(0) == 1001);
    assert(dyn_row.as_text(1) == "Charlie");
    assert(dyn_row.as_double(2) == 3.14159);
    assert(dyn_row.as_blob(3).size() == 9);

    // Deep copy constructor
    SqliteRowDynamic copy_constructed = dyn_row;
    assert(copy_constructed.size() == 4);
    assert(copy_constructed.as_int64(0) == 1001);
    assert(copy_constructed.as_text(1) == "Charlie");

    // Move constructor
    SqliteRowDynamic move_constructed = sqlite_move(dyn_row);
    assert(move_constructed.size() == 4);
    assert(move_constructed.as_int64(0) == 1001);
    assert(move_constructed.as_text(1) == "Charlie");
    assert(dyn_row.empty());
    assert(dyn_row.size() == 0);

    // View extraction from dynamic row
    SqliteRowView dyn_view = move_constructed.view();
    assert(dyn_view.size() == 4);
    assert(dyn_view.as_int64(0) == 1001);
    assert(dyn_view.as_text(1) == "Charlie");
}

// ============================================================================
// 5. Test SqliteRowOwned Template Alias
// ============================================================================
void test_row_owned_template_alias() {
    printf("Testing SqliteRowOwned template alias...\n");

    // SqliteRowOwned<N> (N > 0) -> SqliteRowStatic<N>
    SqliteRowOwned<2> static_alias;
    static_alias[0] = 77LL;
    static_alias[1] = 99.9;
    assert(static_alias.size() == 2);
    assert(static_alias.as_int64(0) == 77);
    assert(static_alias.as_double(1) == 99.9);

    // SqliteRowOwned<0> -> SqliteRowDynamic
    SqliteRowOwned<0> dynamic_alias(2);
    dynamic_alias[0] = SqliteValueOwned::from_text("DynAlias");
    dynamic_alias[1] = 12345LL;
    assert(dynamic_alias.size() == 2);
    assert(dynamic_alias.as_text(0) == "DynAlias");
    assert(dynamic_alias.as_int64(1) == 12345);
}

// ============================================================================
// 6. Test SqliteDatabaseView Integration
// ============================================================================
void test_database_view_integration(sqlite3* db) {
    printf("Testing SqliteDatabaseView statement & row integration...\n");

    SqliteDatabaseView db_view(db);
    assert(db_view.exec("CREATE TABLE orders(order_id INT, customer TEXT, total REAL);") == SQLITE_OK);
    assert(db_view.exec("INSERT INTO orders VALUES(5001, 'Dave', 249.99);") == SQLITE_OK);

    SqliteStatement q = db_view.prepare("SELECT order_id, customer, total FROM orders WHERE order_id = 5001;");
    assert(q.step() == SQLITE_ROW);
    SqliteRowView r = q.row();
    assert(r.as_int64(0) == 5001);
    assert(r.as_text(1) == "Dave");
    assert(r.as_double(2) == 249.99);
}

int main() {
    printf("================================================================\n");
    printf("RUNNING SQLITE ROW TESTS (SqliteRowView, SqliteRowStatic, SqliteRowDynamic)\n");
    printf("================================================================\n");

    sqlite3* db = nullptr;
    assert(sqlite3_open(":memory:", &db) == SQLITE_OK);

    test_row_view_statement(db);
    test_row_view_udf_args();
    test_row_static_stack_allocation(db);
    test_row_dynamic_heap_and_moves();
    test_row_owned_template_alias();
    test_database_view_integration(db);

    sqlite3_close(db);

    printf("================================================================\n");
    printf("ALL SQLITE ROW TESTS PASSED SUCCESSFULLY!\n");
    printf("================================================================\n");
    return 0;
}
