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
    assert(snapshot.as_int() == 101); // default col = 0
    assert(snapshot.as_text(1) == "Alice");

    // 7. Test SqliteRowView default index accessors, hash, and relational comparisons
    assert(row.as_int64() == 101);
    assert(row.as_int() == 101);
    assert(!row.is_null());
    assert(row.type() == SQLITE_INTEGER);
    assert(row.hash() == snapshot.view().hash());
    assert(row == snapshot.view());
    assert(row == SqliteRowOwnedWrapper(snapshot));
    assert(!(row != snapshot));
    assert(!(row < snapshot));
    assert(row <= snapshot);
    assert(row >= snapshot);
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
    SqliteRowOwnedWrapper row(args_arr, 3);

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
    SqliteRowOwnedWrapper dyn_view = move_constructed.view();
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

// ============================================================================
// 7. Test SqliteRowOwnedWrapper & withSqliteRowOwned
// ============================================================================
void test_row_owned_wrapper_and_scope() {
    printf("Testing SqliteRowOwnedWrapper & withSqliteRowOwned...\n");

    // 1. Factory create methods
    SqliteValueOwned val_scalar(42);
    SqliteRowOwnedWrapper w_sc = SqliteRowOwnedWrapper::create(val_scalar);
    assert(w_sc.size() == 1);
    assert(w_sc[0].as_int64() == 42);
    assert(w_sc.hash() == val_scalar.hash());

    SqliteValueOwnedStaticArray<3> static_arr;
    static_arr[0] = SqliteValueOwned(10);
    static_arr[1] = SqliteValueOwned(20);
    static_arr[2] = SqliteValueOwned(30);
    SqliteRowOwnedWrapper w_arr = SqliteRowOwnedWrapper::create(static_arr);
    assert(w_arr.size() == 3);
    assert(w_arr[1].as_int64() == 20);

    // 2. withSqliteRowOwned runtime stack dispatcher (1..8)
    int cols = 3;
    int res = withSqliteRowOwned(cols, [](SqliteRowOwnedWrapper wrapper) {
        assert(wrapper.size() == 3);
        wrapper[0] = SqliteValueOwned(100);
        wrapper[1] = SqliteValueOwned::from_text("WrapperText");
        wrapper[2] = SqliteValueOwned(3.14159);

        // Direct typed as_* accessors on SqliteRowOwnedWrapper
        assert(wrapper.as_int64(0) == 100);
        assert(wrapper.as_int(0) == 100);
        assert(wrapper.as_text(1) == "WrapperText");
        assert(wrapper.as_double(2) == 3.14159);
        assert(!wrapper.is_null(0));
        assert(wrapper.type(0) == SQLITE_INTEGER);
        assert(wrapper.type(1) == SQLITE_TEXT);
        assert(wrapper.type(2) == SQLITE_FLOAT);
        return 999;
    });
    assert(res == 999);
}

// ============================================================================
// 8. Exhaustive Test Suite for withSqliteRowOwned (0..64 columns)
// ============================================================================
void test_with_sqlite_row_owned_exhaustive() {
    printf("Testing withSqliteRowOwned exhaustive boundary tests (0..64 columns)...\n");

    // Test zero & negative bounds
    int ret_zero = withSqliteRowOwned(0, [](SqliteRowOwnedWrapper wrapper) {
        assert(wrapper.size() == 0);
        assert(wrapper.empty());
        assert(wrapper.data() == nullptr);
        return 100;
    });
    assert(ret_zero == 100);

    int ret_neg = withSqliteRowOwned(-5, [](SqliteRowOwnedWrapper wrapper) {
        assert(wrapper.size() == 0);
        assert(wrapper.empty());
        assert(wrapper.data() == nullptr);
        return 200;
    });
    assert(ret_neg == 200);

    // Test all stack sizes 1 through 8
    for (int k = 1; k <= 8; ++k) {
        int count_checked = withSqliteRowOwned(k, [k](SqliteRowOwnedWrapper wrapper) {
            assert(wrapper.size() == k);
            assert(!wrapper.empty());
            assert(wrapper.data() != nullptr);

            for (int i = 0; i < k; ++i) {
                if (i % 3 == 0) {
                    wrapper[i] = SqliteValueOwned(i * 100LL);
                } else if (i % 3 == 1) {
                    wrapper[i] = SqliteValueOwned::from_text("stack_col");
                } else {
                    wrapper[i] = SqliteValueOwned(i * 1.5);
                }
            }

            for (int i = 0; i < k; ++i) {
                if (i % 3 == 0) {
                    assert(wrapper[i].as_int64() == i * 100LL);
                } else if (i % 3 == 1) {
                    assert(wrapper[i].as_text() == "stack_col");
                } else {
                    assert(wrapper[i].as_double() == i * 1.5);
                }
            }
            return k * 10;
        });
        assert(count_checked == k * 10);
    }

    // Test dynamic heap fallback sizes (> 8)
    const int heap_test_sizes[] = { 9, 10, 16, 32, 64 };
    for (size_t s = 0; s < sizeof(heap_test_sizes) / sizeof(heap_test_sizes[0]); ++s) {
        int k = heap_test_sizes[s];
        bool heap_ok = withSqliteRowOwned(k, [k](SqliteRowOwnedWrapper wrapper) {
            assert(wrapper.size() == k);
            assert(!wrapper.empty());
            assert(wrapper.data() != nullptr);

            for (int i = 0; i < k; ++i) {
                wrapper[i] = SqliteValueOwned(i + 1000LL);
            }

            for (int i = 0; i < k; ++i) {
                assert(wrapper[i].as_int64() == i + 1000LL);
            }

            // Test hash computation over large dynamic row
            unsigned long long h = wrapper.hash();
            assert(h != 0);
            return true;
        });
        assert(heap_ok);
    }
}

static void test_row_view_transparent_relational_operators(sqlite3* db) {
    printf("Testing SqliteRowView Transparent Relational Operators...\n");

    sqlite3_stmt* stmt = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT 42, 'hello', 3.14;", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);

    SqliteRowView row(stmt);
    assert(row.size() == 3);

    // 1. Single scalar row from 1-column statement
    sqlite3_stmt* stmt1 = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT 100;", -1, &stmt1, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt1) == SQLITE_ROW);
    SqliteRowView row1(stmt1);
    assert(row1.size() == 1);

    // Transparent comparisons against primitive integers
    assert(row1 == 100LL);
    assert(row1 == 100);
    assert(row1 != 99);
    assert(row1 < 101);
    assert(row1 <= 100);
    assert(row1 > 50);
    assert(row1 >= 100);

    // Symmetric reverse operators
    assert(100LL == row1);
    assert(100 == row1);
    assert(99 != row1);
    assert(101 > row1);
    assert(100 >= row1);
    assert(50 < row1);
    assert(100 <= row1);

    // Transparent comparisons against SqliteValueOwned & SqliteValueView
    SqliteValueOwned val_100(100LL);
    SqliteValueOwned val_50(50LL);
    assert(row1 == val_100);
    assert(row1 != val_50);
    assert(row1 > val_50);
    assert(val_100 == row1);
    assert(val_50 < row1);

    SqliteValueView view_100 = row1[0];
    assert(row1 == view_100);
    assert(view_100 == row1);

    // 2. Single text row
    sqlite3_stmt* stmt_str = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT 'sqlite';", -1, &stmt_str, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt_str) == SQLITE_ROW);
    SqliteRowView row_str(stmt_str);
    assert(row_str.size() == 1);

    assert(row_str == "sqlite");
    assert(row_str != "other");
    assert("sqlite" == row_str);
    assert("other" != row_str);

    SqliteStringView sv("sqlite");
    assert(row_str == sv);
    assert(sv == row_str);

    SqliteStringOwned so("sqlite");
    assert(row_str == so);
    assert(so == row_str);

    // 3. Single double, bool, and blob rows
    sqlite3_stmt* stmt_dbl = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT 3.14159;", -1, &stmt_dbl, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt_dbl) == SQLITE_ROW);
    SqliteRowView row_dbl(stmt_dbl);
    assert(row_dbl == 3.14159);
    assert(3.14159 == row_dbl);
    assert(row_dbl > 2.0);
    assert(2.0 < row_dbl);

    sqlite3_stmt* stmt_bool = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT 1;", -1, &stmt_bool, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt_bool) == SQLITE_ROW);
    SqliteRowView row_bool(stmt_bool);
    assert(row_bool == true);
    assert(true == row_bool);

    sqlite3_stmt* stmt_blob = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT X'01020304';", -1, &stmt_blob, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt_blob) == SQLITE_ROW);
    SqliteRowView row_blob(stmt_blob);
    const uint8_t blob_bytes[] = {0x01, 0x02, 0x03, 0x04};
    SqliteBlobView bv(blob_bytes, 4);
    assert(row_blob == bv);
    assert(bv == row_blob);
    SqliteBlobOwned bo(bv.data(), bv.size());
    assert(row_blob == bo);
    assert(bo == row_blob);

    // 4. Multi-column comparisons against SqliteRowOwnedWrapper, and other SqliteRowView
    SqliteValueOwned vals[3];
    vals[0] = 42LL;
    vals[1] = SqliteValueOwned::from_text("hello");
    vals[2] = 3.14;
    SqliteRowOwnedWrapper wrapper(vals, 3);

    assert(row == wrapper);
    assert(wrapper == row);
    assert(!(row != wrapper));
    assert(row <= wrapper);
    assert(row >= wrapper);

    // Lexicographical ordering checks
    SqliteValueOwned vals_smaller[3];
    vals_smaller[0] = 41LL;
    vals_smaller[1] = SqliteValueOwned::from_text("hello");
    vals_smaller[2] = 3.14;
    SqliteRowOwnedWrapper wrap_smaller(vals_smaller, 3);
    assert(wrap_smaller < row);
    assert(row > wrap_smaller);
    assert(wrap_smaller <= row);
    assert(row >= wrap_smaller);

    // SqliteRowView vs SqliteRowView (Stmt vs ViewArray)
    SqliteValueView view_array_elems[3] = { row[0], row[1], row[2] };
    SqliteRowView row_from_view_arr(view_array_elems, 3);
    assert(row == row_from_view_arr);
    assert(row_from_view_arr == row);
    assert(row <= row_from_view_arr);
    assert(row >= row_from_view_arr);

    // Empty SqliteRowView comparisons
    SqliteRowView empty_row;
    assert(empty_row.size() == 0);
    assert(empty_row < row);
    assert(row > empty_row);

    sqlite3_finalize(stmt_dbl);
    sqlite3_finalize(stmt_bool);
    sqlite3_finalize(stmt_blob);
    sqlite3_finalize(stmt1);
    sqlite3_finalize(stmt_str);
    sqlite3_finalize(stmt);
}

// ============================================================================
// 10. Test Range-Based For Loop Iterators on all Row Types
// ============================================================================
void test_row_iterators(sqlite3* db) {
    printf("Testing Range-Based For Loop Iterators on all Row Types...\n");

    // 1. SqliteRowView Iterator over SQLite Statement
    SqliteStatement stmt(db, "SELECT 10, 'Hello', 3.14;");
    assert(stmt.step() == SQLITE_ROW);
    SqliteRowView r_view = stmt.row();
    assert(r_view.size() == 3);

    int view_count = 0;
    for (SqliteValueView col : r_view) {
        assert(!col.is_null());
        view_count++;
    }
    assert(view_count == 3);

    // 2. SqliteRowStatic<3> Iterator
    SqliteRowStatic<3> s_row;
    s_row[0] = 1LL;
    s_row[1] = 2LL;
    s_row[2] = 3LL;
    sqlite3_int64 s_sum = 0;
    int s_count = 0;
    for (const SqliteValueOwned& val : s_row) {
        s_sum += val.as_int64();
        s_count++;
    }
    assert(s_count == 3);
    assert(s_sum == 6);

    // 3. SqliteRowDynamic Iterator
    SqliteRowDynamic d_row(4);
    d_row[0] = 10LL;
    d_row[1] = 20LL;
    d_row[2] = 30LL;
    d_row[3] = 40LL;
    sqlite3_int64 d_sum = 0;
    int d_count = 0;
    for (const SqliteValueOwned& val : d_row) {
        d_sum += val.as_int64();
        d_count++;
    }
    assert(d_count == 4);
    assert(d_sum == 100);

    // 4. SqliteRowOwnedWrapper Iterator
    SqliteRowOwnedWrapper wrap = d_row.view();
    sqlite3_int64 w_sum = 0;
    int w_count = 0;
    for (const SqliteValueOwned& val : wrap) {
        w_sum += val.as_int64();
        w_count++;
    }
    assert(w_count == 4);
    assert(w_sum == 100);

    // 5. SqliteRowOwned<N> Template Alias Iterators
    SqliteRowOwned<2> alias_static;
    alias_static[0] = 5LL;
    alias_static[1] = 15LL;
    sqlite3_int64 a_sum = 0;
    for (const SqliteValueOwned& val : alias_static) {
        a_sum += val.as_int64();
    }
    assert(a_sum == 20);

    SqliteRowOwned<0> alias_dyn(2);
    alias_dyn[0] = 100LL;
    alias_dyn[1] = 200LL;
    sqlite3_int64 ad_sum = 0;
    for (const SqliteValueOwned& val : alias_dyn) {
        ad_sum += val.as_int64();
    }
    assert(ad_sum == 300);

    // 6. Empty Row Iterator
    SqliteRowView empty_view;
    int e_count = 0;
    for (SqliteValueView col : empty_view) {
        (void)col;
        e_count++;
    }
    assert(e_count == 0);
    assert(empty_view.begin() == empty_view.end());

    SqliteRowDynamic empty_dyn(0);
    for (const SqliteValueOwned& val : empty_dyn) {
        (void)val;
        e_count++;
    }
    assert(e_count == 0);
    assert(empty_dyn.begin() == empty_dyn.end());
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
    test_row_owned_wrapper_and_scope();
    test_with_sqlite_row_owned_exhaustive();
    test_row_view_transparent_relational_operators(db);
    test_row_iterators(db);

    sqlite3_close(db);

    printf("================================================================\n");
    printf("ALL SQLITE ROW TESTS PASSED SUCCESSFULLY!\n");
    printf("================================================================\n");
    return 0;
}
