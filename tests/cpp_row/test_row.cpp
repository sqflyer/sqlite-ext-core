#define SQLITE_CORE
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sqlite3.h>
#include "sqlite3_statement.hpp"
#include "sqlite3_row.hpp"
#include "sqlite3_value_containers.hpp"
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

    // 6. Materialize snapshot into owned SqliteValueVec<8>
    SqliteValueVec<8> snapshot;
    snapshot.resize(row.size());
    for (int i = 0; i < row.size(); ++i) snapshot[i] = row[i].to_owned();

    assert(snapshot.size() == 5);
    assert(snapshot.data()[0].as_int64() == 101);
    assert(snapshot.data()[1].as_text() == "Alice");

    // 7. Test SqliteRowView default index accessors, hash, and relational comparisons
    assert(row.as_int64() == 101);
    assert(row.as_int() == 101);
    assert(!row.is_null());
    assert(row.type() == SQLITE_INTEGER);
    assert(row.hash() == snapshot.view().hash());
    assert(row == snapshot.view());
    assert(row == SqliteRowOwnedWrapper(snapshot));
    assert(!(row != snapshot.view()));
    assert(!(row < snapshot.view()));
    assert(row <= snapshot.view());
    assert(row >= snapshot.view());
}

// ============================================================================
// 2. Test SqliteRowView over SqliteUdfArgs
// ============================================================================
void test_row_view_udf_args() {
    printf("Testing SqliteRowView over SqliteUdfArgs & contiguous spans...\n");

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
}

// ============================================================================
// 3. Test SqliteValueTuple Stack Allocation (0 Mallocs)
// ============================================================================
void test_row_tuple_stack_allocation(sqlite3* db) {
    printf("Testing SqliteValueTuple stack allocation...\n");

    // Exact footprint check: 3 columns * 16 bytes = 48 bytes!
    static_assert(sizeof(SqliteValueTuple<3>) == 48, "SqliteValueTuple<3> must be exactly 48 bytes!");
    static_assert(sizeof(SqliteValueTuple<4>) == 64, "SqliteValueTuple<4> must be exactly 64 bytes!");

    SqliteValueTuple<3> stack_row;
    assert(stack_row.size() == 3);
    assert(stack_row[0].is_null());

    stack_row[0] = SqliteValueOwned(202LL);
    stack_row[1] = SqliteValueOwned::from_text("Bob");
    stack_row[2] = SqliteValueOwned(88.0);

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

    // Construct static tuple from view
    SqliteValueTuple<3> copied_from_view;
    for (int i = 0; i < 3; ++i) copied_from_view[i] = q_row[i].to_owned();
    assert(copied_from_view.size() == 3);
    assert(copied_from_view[0].as_int64() == 202);
    assert(copied_from_view[1].as_text() == "Bob");
    assert(copied_from_view[2].as_double() == 88.0);
}

// ============================================================================
// 4. Test SqliteValueVec Adaptive Heap & Move Semantics
// ============================================================================
void test_row_vec_heap_and_moves() {
    printf("Testing SqliteValueVec heap allocation and move semantics...\n");

    SqliteValueVec<4> dyn_row(4);
    assert(dyn_row.size() == 4);
    assert(dyn_row.column_count() == 4);
    assert(!dyn_row.empty());

    dyn_row[0] = SqliteValueOwned(1001LL);
    dyn_row[1] = SqliteValueOwned::from_text("Charlie");
    dyn_row[2] = SqliteValueOwned(3.14159);
    dyn_row[3] = SqliteValueOwned::from_blob("BLOB_DATA", 9);

    assert(dyn_row[0].as_int64() == 1001);
    assert(dyn_row[1].as_text() == "Charlie");
    assert(dyn_row[2].as_double() == 3.14159);
    assert(dyn_row[3].as_blob().size() == 9);

    // Deep copy constructor
    SqliteValueVec<4> copy_constructed = dyn_row;
    assert(copy_constructed.size() == 4);
    assert(copy_constructed[0].as_int64() == 1001);
    assert(copy_constructed[1].as_text() == "Charlie");

    // Move constructor
    SqliteValueVec<4> move_constructed = sqlite_move(dyn_row);
    assert(move_constructed.size() == 4);
    assert(move_constructed[0].as_int64() == 1001);
    assert(move_constructed[1].as_text() == "Charlie");
    assert(dyn_row.empty());
    assert(dyn_row.size() == 0);

    // View extraction from dynamic row
    SqliteRowOwnedWrapper dyn_view = move_constructed.view();
    assert(dyn_view.size() == 4);
    assert(dyn_view.as_int64(0) == 1001);
    assert(dyn_view.as_text(1) == "Charlie");
}

// ============================================================================
// 5. Test SqliteDatabaseView Integration
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
// 6. Test SqliteRowOwnedWrapper & withSqliteRowOwned
// ============================================================================
void test_row_owned_wrapper_and_scope() {
    printf("Testing SqliteRowOwnedWrapper & withSqliteRowOwned...\n");

    // 1. Factory create methods
    SqliteValueOwned val_scalar(42);
    SqliteRowOwnedWrapper w_sc = SqliteRowOwnedWrapper::create(val_scalar);
    assert(w_sc.size() == 1);
    assert(w_sc[0].as_int64() == 42);
    assert(w_sc.hash() == val_scalar.hash());

    SqliteValueTuple<3> static_arr;
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
// 7. Exhaustive Test Suite for withSqliteRowOwned (0..64 columns)
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

template <typename L, typename R>
static void assert_row_relops_equal(const L& left, const R& right) {
    assert(left == right);
    assert(right == left);
    assert(!(left != right));
    assert(!(right != left));
    assert(left <= right);
    assert(right <= left);
    assert(left >= right);
    assert(right >= left);
    assert(!(left < right));
    assert(!(right < left));
    assert(!(left > right));
    assert(!(right > left));
}

template <typename L, typename R>
static void assert_row_relops_less(const L& left, const R& right) {
    assert(left < right);
    assert(left <= right);
    assert(left != right);
    assert(!(left == right));
    assert(!(left > right));
    assert(!(left >= right));

    // Reverse orientation
    assert(right > left);
    assert(right >= left);
    assert(right != left);
    assert(!(right == left));
    assert(!(right < left));
    assert(!(right <= left));
}

static void test_row_view_transparent_relational_operators(sqlite3* db) {
    printf("Testing SqliteRowView & SqliteRowOwnedWrapper Comprehensive Relational Operators...\n");

    // ========================================================================
    // A. Multi-Column Row Representations Matrix: (10, "alpha", 3.14)
    // ========================================================================
    sqlite3_stmt* stmt = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT 10, 'alpha', 3.14;", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    SqliteRowView rview(stmt);

    sqlite3_stmt* stmt_dup = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT 10, 'alpha', 3.14;", -1, &stmt_dup, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt_dup) == SQLITE_ROW);
    SqliteRowView rview_dup(stmt_dup);

    SqliteValueOwned arr[3];
    arr[0] = SqliteValueOwned(10);
    arr[1] = SqliteValueOwned("alpha");
    arr[2] = SqliteValueOwned(3.14);
    SqliteRowOwnedWrapper wrap(arr, 3);

    SqliteValueView view_array_elems[3] = { rview[0], rview[1], rview[2] };
    SqliteRowView rview_from_arr(view_array_elems, 3);

    // Equality matrix across representations
    assert_row_relops_equal(rview, rview);
    assert_row_relops_equal(rview, rview_dup);
    assert_row_relops_equal(rview, wrap);
    assert_row_relops_equal(rview, rview_from_arr);
    assert_row_relops_equal(wrap, wrap);
    assert_row_relops_equal(wrap, rview_from_arr);

    // Hash code equivalences
    assert(rview.hash() == rview_dup.hash());
    assert(rview.hash() == wrap.hash());
    assert(rview.hash() == rview_from_arr.hash());

    // ========================================================================
    // B. Lexicographical Ordering Differences Matrix (First, Middle, Last)
    // ========================================================================
    // Difference in last column (3.14 vs 3.15)
    SqliteValueOwned arr_last_greater[3];
    arr_last_greater[0] = SqliteValueOwned(10);
    arr_last_greater[1] = SqliteValueOwned("alpha");
    arr_last_greater[2] = SqliteValueOwned(3.15);
    SqliteRowOwnedWrapper wrap_last_greater(arr_last_greater, 3);
    assert_row_relops_less(rview, wrap_last_greater);
    assert_row_relops_less(wrap, wrap_last_greater);

    // Difference in middle column ("alpha" vs "beta")
    SqliteValueOwned arr_mid_greater[3];
    arr_mid_greater[0] = SqliteValueOwned(10);
    arr_mid_greater[1] = SqliteValueOwned("beta");
    arr_mid_greater[2] = SqliteValueOwned(1.0);
    SqliteRowOwnedWrapper wrap_mid_greater(arr_mid_greater, 3);
    assert_row_relops_less(rview, wrap_mid_greater);
    assert_row_relops_less(wrap, wrap_mid_greater);

    // Difference in first column (10 vs 11)
    SqliteValueOwned arr_first_greater[3];
    arr_first_greater[0] = SqliteValueOwned(11);
    arr_first_greater[1] = SqliteValueOwned("aaa");
    arr_first_greater[2] = SqliteValueOwned(0.0);
    SqliteRowOwnedWrapper wrap_first_greater(arr_first_greater, 3);
    assert_row_relops_less(rview, wrap_first_greater);
    assert_row_relops_less(wrap, wrap_first_greater);

    // ========================================================================
    // C. Arity / Length Prefix Semantics
    // ========================================================================
    SqliteValueOwned arr_prefix[2];
    arr_prefix[0] = SqliteValueOwned(10);
    arr_prefix[1] = SqliteValueOwned("alpha");
    SqliteRowOwnedWrapper wrap_prefix(arr_prefix, 2);

    assert_row_relops_less(wrap_prefix, rview);
    assert_row_relops_less(wrap_prefix, wrap);

    SqliteRowView empty_row_view;
    SqliteRowOwnedWrapper empty_row_wrap(nullptr, 0);
    assert_row_relops_equal(empty_row_view, empty_row_wrap);
    assert_row_relops_less(empty_row_view, wrap_prefix);
    assert_row_relops_less(empty_row_wrap, rview);

    // ========================================================================
    // D. SQLite Type-Rank Collation in Multi-Column Rows
    // ========================================================================
    SqliteValueOwned arr_null[2];
    SqliteRowOwnedWrapper wrap_null(arr_null, 2);
    SqliteValueOwned arr_int_pair[2];
    arr_int_pair[0] = SqliteValueOwned(10);
    arr_int_pair[1] = SqliteValueOwned(20);
    SqliteRowOwnedWrapper wrap_int_pair(arr_int_pair, 2);
    assert_row_relops_less(wrap_null, wrap_int_pair);

    // Strict integer vs float tie-breaker
    SqliteValueOwned arr_dbl_pair[2];
    arr_dbl_pair[0] = SqliteValueOwned(10.0);
    arr_dbl_pair[1] = SqliteValueOwned(20.0);
    SqliteRowOwnedWrapper wrap_dbl_pair(arr_dbl_pair, 2);
    assert_row_relops_less(wrap_int_pair, wrap_dbl_pair);
    assert(wrap_int_pair != wrap_dbl_pair);

    // Number vs Text ('10' vs 10): 10 < '10'
    SqliteValueOwned val_10(10);
    SqliteValueOwned val_str_10("10");
    SqliteRowOwnedWrapper wrap_num = SqliteRowOwnedWrapper::create(val_10);
    SqliteRowOwnedWrapper wrap_txt = SqliteRowOwnedWrapper::create(val_str_10);
    assert_row_relops_less(wrap_num, wrap_txt);

    // Text vs Blob ('10' vs blob'10'): '10' < blob'10'
    const uint8_t blob_10_bytes[] = { '1', '0' };
    SqliteBlobView bv_10(blob_10_bytes, 2);
    SqliteValueOwned val_blob_10(bv_10);
    SqliteRowOwnedWrapper wrap_blob_10 = SqliteRowOwnedWrapper::create(val_blob_10);
    assert_row_relops_less(wrap_txt, wrap_blob_10);

    // ========================================================================
    // E. 1-Column Row Relational Operators across ALL Fundamental Types
    // ========================================================================
    sqlite3_stmt* stmt_int1 = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT 42;", -1, &stmt_int1, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt_int1) == SQLITE_ROW);
    SqliteRowView row_int1(stmt_int1);

    SqliteValueOwned val_42(42);
    SqliteRowOwnedWrapper wrap_int1 = SqliteRowOwnedWrapper::create(val_42);

    // Signed and unsigned integer overloads in both orientations
    assert_row_relops_equal(row_int1, 42);
    assert_row_relops_equal(row_int1, 42LL);
    assert_row_relops_equal(row_int1, 42u);
    assert_row_relops_equal(row_int1, 42UL);
    assert_row_relops_equal(row_int1, 42ULL);
    assert(row_int1 != 42.0);
    assert_row_relops_less(row_int1, 50);
    assert_row_relops_less(row_int1, 50u);

    assert_row_relops_equal(wrap_int1, 42);
    assert_row_relops_equal(wrap_int1, 42LL);
    assert_row_relops_equal(wrap_int1, 42u);
    assert_row_relops_equal(wrap_int1, 42UL);
    assert_row_relops_equal(wrap_int1, 42ULL);
    assert(wrap_int1 != 42.0);
    assert_row_relops_less(wrap_int1, 50);

    // Direct comparison against SqliteValueOwned and SqliteValueView
    assert_row_relops_equal(row_int1, val_42);
    assert_row_relops_equal(wrap_int1, val_42);
    assert_row_relops_equal(row_int1, row_int1[0]);
    assert_row_relops_equal(wrap_int1, row_int1[0]);

    // Floating-point 1-column row
    sqlite3_stmt* stmt_dbl = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT 42.0;", -1, &stmt_dbl, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt_dbl) == SQLITE_ROW);
    SqliteRowView row_dbl(stmt_dbl);

    SqliteValueOwned val_42_dbl(42.0);
    SqliteRowOwnedWrapper wrap_dbl = SqliteRowOwnedWrapper::create(val_42_dbl);

    assert_row_relops_equal(row_dbl, 42.0);
    assert_row_relops_equal(row_dbl, 42.0f);
    assert(row_dbl != 42);
    assert_row_relops_less(row_dbl, 50.0);
    assert_row_relops_less(row_dbl, 50.0f);

    assert_row_relops_equal(wrap_dbl, 42.0);
    assert_row_relops_equal(wrap_dbl, 42.0f);
    assert(wrap_dbl != 42);
    assert_row_relops_less(wrap_dbl, 50.0);

    // Text 1-column row
    sqlite3_stmt* stmt_str = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT 'delta';", -1, &stmt_str, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt_str) == SQLITE_ROW);
    SqliteRowView row_str(stmt_str);

    SqliteValueOwned val_delta("delta");
    SqliteRowOwnedWrapper wrap_str = SqliteRowOwnedWrapper::create(val_delta);

    assert_row_relops_equal(row_str, "delta");
    assert_row_relops_equal(row_str, SqliteStringView("delta"));
    SqliteStringOwned str_owned("delta");
    assert_row_relops_equal(row_str, str_owned);
    assert_row_relops_less(row_str, "echo");
    assert_row_relops_less(row_str, SqliteStringView("echo"));

    assert_row_relops_equal(wrap_str, "delta");
    assert_row_relops_equal(wrap_str, SqliteStringView("delta"));
    assert_row_relops_equal(wrap_str, str_owned);
    assert_row_relops_less(wrap_str, "echo");

    // Boolean 1-column row
    sqlite3_stmt* stmt_bool = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT 1;", -1, &stmt_bool, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt_bool) == SQLITE_ROW);
    SqliteRowView row_bool(stmt_bool);
    assert_row_relops_equal(row_bool, true);
    assert_row_relops_equal(row_bool, 1);

    // Blob 1-column row
    sqlite3_stmt* stmt_blob = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT X'01020304';", -1, &stmt_blob, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt_blob) == SQLITE_ROW);
    SqliteRowView row_blob(stmt_blob);
    const uint8_t blob_bytes[] = { 0x01, 0x02, 0x03, 0x04 };
    SqliteBlobView bv(blob_bytes, 4);
    assert_row_relops_equal(row_blob, bv);
    SqliteBlobOwned bo(bv.data(), bv.size());
    assert_row_relops_equal(row_blob, bo);

    SqliteValueOwned val_blob(bv);
    SqliteRowOwnedWrapper wrap_blob = SqliteRowOwnedWrapper::create(val_blob);
    assert_row_relops_equal(wrap_blob, bv);
    assert_row_relops_equal(wrap_blob, bo);

    // Cleanup
    sqlite3_finalize(stmt_dbl);
    sqlite3_finalize(stmt_bool);
    sqlite3_finalize(stmt_blob);
    sqlite3_finalize(stmt_int1);
    sqlite3_finalize(stmt_str);
    sqlite3_finalize(stmt_dup);
    sqlite3_finalize(stmt);
}

// ============================================================================
// 8. Test Range-Based For Loop Iterators on all Row Types
// ============================================================================
void test_row_iterators(sqlite3* db) {
    printf("Testing Range-Based For Loop Iterators on Row Types...\n");

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

    // 2. SqliteRowOwnedWrapper Iterator
    SqliteValueOwned d_row[4];
    d_row[0] = 10LL;
    d_row[1] = 20LL;
    d_row[2] = 30LL;
    d_row[3] = 40LL;
    SqliteRowOwnedWrapper wrap(d_row, 4);
    sqlite3_int64 w_sum = 0;
    int w_count = 0;
    for (const SqliteValueOwned& val : wrap) {
        w_sum += val.as_int64();
        w_count++;
    }
    assert(w_count == 4);
    assert(w_sum == 100);

    // 3. Empty Row Iterator
    SqliteRowView empty_view;
    int e_count = 0;
    for (SqliteValueView col : empty_view) {
        (void)col;
        e_count++;
    }
    assert(e_count == 0);
    assert(empty_view.begin() == empty_view.end());
}

int main() {
    printf("================================================================\n");
    printf("RUNNING SQLITE ROW TESTS (SqliteRowView, SqliteRowOwnedWrapper)\n");
    printf("================================================================\n");

    sqlite3* db = nullptr;
    assert(sqlite3_open(":memory:", &db) == SQLITE_OK);

    test_row_view_statement(db);
    test_row_view_udf_args();
    test_row_tuple_stack_allocation(db);
    test_row_vec_heap_and_moves();
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
