#define SQLITE_CORE
#include "sqlite3_db.hpp"
#include "sqlite3_row.hpp"
#include "sqlite3_statement.hpp"
#include "sqlite3_value_containers.hpp"
#include <assert.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// 1. Test SqliteRowView over Prepared Statement
// ============================================================================
void test_row_view_statement(sqlite3 *db) {
  printf("Testing SqliteRowView over SqliteStatement...\n");

  assert(sqlite3_exec(db,
                      "CREATE TABLE users(id INT, name TEXT, score REAL, "
                      "avatar BLOB, extra JSON);",
                      nullptr, nullptr, nullptr) == SQLITE_OK);
  assert(sqlite3_exec(db,
                      "INSERT INTO users VALUES(101, 'Alice', 98.5, "
                      "X'DEADBEEF', '{\"role\":\"admin\"}');",
                      nullptr, nullptr, nullptr) == SQLITE_OK);

  SqliteStatement stmt(
      db, "SELECT id, name, score, avatar, extra FROM users WHERE id = 101;");
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
    if (col_idx == 0)
      assert(val.as_int64() == 101);
    if (col_idx == 1)
      assert(val.as_text() == "Alice");
    if (col_idx == 2)
      assert(val.as_double() == 98.5);
    if (col_idx == 3)
      assert(val.as_blob().size() == 4);
    if (col_idx == 4)
      assert(val.as_text() == "{\"role\":\"admin\"}");
    col_idx++;
  }
  assert(col_idx == 5);

  // 6. Materialize snapshot into owned SqliteValueVec<8>
  SqliteValueVec<8> snapshot;
  snapshot.resize(row.size());
  for (int i = 0; i < row.size(); ++i)
    snapshot[i] = row[i].to_owned();

  assert(snapshot.size() == 5);
  assert(snapshot.data()[0].as_int64() == 101);
  assert(snapshot.data()[1].as_text() == "Alice");

  // 7. Test SqliteRowView default index accessors, hash, and relational
  // comparisons
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
  SqliteValueOwned args_arr[3] = {sqlite_move(v1), sqlite_move(v2),
                                  sqlite_move(v3)};
  SqliteRowOwnedWrapper row(args_arr, 3);

  assert(row.size() == 3);
  assert(row.as_int64(0) == 42);
  assert(row.as_text(1) == "hello");
  assert(row.as_double(2) == 3.14);
}

// ============================================================================
// 3. Test SqliteValueTuple Stack Allocation (0 Mallocs)
// ============================================================================
void test_row_tuple_stack_allocation(sqlite3 *db) {
  printf("Testing SqliteValueTuple stack allocation...\n");

  // Exact footprint check: 3 columns * 16 bytes = 48 bytes!
  static_assert(sizeof(SqliteValueTuple<3>) == 48,
                "SqliteValueTuple<3> must be exactly 48 bytes!");
  static_assert(sizeof(SqliteValueTuple<4>) == 64,
                "SqliteValueTuple<4> must be exactly 64 bytes!");

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
  assert(sqlite3_exec(db, "CREATE TABLE staff(id INT, name TEXT, rating REAL);",
                      nullptr, nullptr, nullptr) == SQLITE_OK);

  SqliteStatement insert_stmt(db, "INSERT INTO staff VALUES(?, ?, ?);");
  assert(insert_stmt.bind(1, stack_row[0]) == SQLITE_OK);
  assert(insert_stmt.bind(2, stack_row[1]) == SQLITE_OK);
  assert(insert_stmt.bind(3, stack_row[2]) == SQLITE_OK);
  assert(insert_stmt.step() == SQLITE_DONE);

  // Verify row was inserted correctly
  SqliteStatement query_stmt(
      db, "SELECT id, name, rating FROM staff WHERE id = 202;");
  assert(query_stmt.step() == SQLITE_ROW);
  SqliteRowView q_row = query_stmt.row();
  assert(q_row.as_int64(0) == 202);
  assert(q_row.as_text(1) == "Bob");
  assert(q_row.as_double(2) == 88.0);

  // Construct static tuple from view
  SqliteValueTuple<3> copied_from_view;
  for (int i = 0; i < 3; ++i)
    copied_from_view[i] = q_row[i].to_owned();
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
void test_database_view_integration(sqlite3 *db) {
  printf("Testing SqliteDatabaseView statement & row integration...\n");

  SqliteDatabaseView db_view(db);
  assert(db_view.exec(
             "CREATE TABLE orders(order_id INT, customer TEXT, total REAL);") ==
         SQLITE_OK);
  assert(db_view.exec("INSERT INTO orders VALUES(5001, 'Dave', 249.99);") ==
         SQLITE_OK);

  SqliteStatement q = db_view.prepare(
      "SELECT order_id, customer, total FROM orders WHERE order_id = 5001;");
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
  printf("Testing withSqliteRowOwned exhaustive boundary tests (0..64 "
         "columns)...\n");

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
    int count_checked =
        withSqliteRowOwned(k, [k](SqliteRowOwnedWrapper wrapper) {
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

  // Test out-of-bounds write absorption on immutable fallback null
  withSqliteRowOwned(4, [](SqliteRowOwnedWrapper wrapper) {
    wrapper[-1] = 9999LL;
    wrapper[10] = SqliteValueOwned::from_text("should_be_dropped");
    assert(wrapper[-1].is_null());
    assert(wrapper[-1].is_immutable());
    assert(wrapper[10].is_null());
    assert(wrapper[10].is_immutable());
    return 0;
  });

  // Test dynamic heap fallback sizes (> 8)
  const int heap_test_sizes[] = {9, 10, 16, 32, 64};
  for (size_t s = 0; s < sizeof(heap_test_sizes) / sizeof(heap_test_sizes[0]);
       ++s) {
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
static void assert_row_relops_equal(const L &left, const R &right) {
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
static void assert_row_relops_less(const L &left, const R &right) {
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

static void test_row_view_transparent_relational_operators(sqlite3 *db) {
  printf("Testing SqliteRowView & SqliteRowOwnedWrapper Comprehensive "
         "Relational Operators...\n");

  // ========================================================================
  // A. Multi-Column Row Representations Matrix: (10, "alpha", 3.14)
  // ========================================================================
  sqlite3_stmt *stmt = nullptr;
  assert(sqlite3_prepare_v2(db, "SELECT 10, 'alpha', 3.14;", -1, &stmt,
                            nullptr) == SQLITE_OK);
  assert(sqlite3_step(stmt) == SQLITE_ROW);
  SqliteRowView rview(stmt);

  sqlite3_stmt *stmt_dup = nullptr;
  assert(sqlite3_prepare_v2(db, "SELECT 10, 'alpha', 3.14;", -1, &stmt_dup,
                            nullptr) == SQLITE_OK);
  assert(sqlite3_step(stmt_dup) == SQLITE_ROW);
  SqliteRowView rview_dup(stmt_dup);

  SqliteValueOwned arr[3];
  arr[0] = SqliteValueOwned(10);
  arr[1] = SqliteValueOwned("alpha");
  arr[2] = SqliteValueOwned(3.14);
  SqliteRowOwnedWrapper wrap(arr, 3);

  SqliteValueView view_array_elems[3] = {rview[0], rview[1], rview[2]};
  SqliteRowView rview_from_arr(view_array_elems, 3);

  const SqliteValueView *view_ptr_elems[3] = {
      &view_array_elems[0], &view_array_elems[1], &view_array_elems[2]};
  SqliteRowView rview_from_ptr_arr(view_ptr_elems, 3);

  // Equality matrix across representations
  assert_row_relops_equal(rview, rview);
  assert_row_relops_equal(rview, rview_dup);
  assert_row_relops_equal(rview, wrap);
  assert_row_relops_equal(rview, rview_from_arr);
  assert_row_relops_equal(rview, rview_from_ptr_arr);
  assert_row_relops_equal(wrap, wrap);
  assert_row_relops_equal(wrap, rview_from_arr);
  assert_row_relops_equal(wrap, rview_from_ptr_arr);
  assert_row_relops_equal(rview_from_arr, rview_from_ptr_arr);

  // Hash code equivalences
  assert(rview.hash() == rview_dup.hash());
  assert(rview.hash() == wrap.hash());
  assert(rview.hash() == rview_from_arr.hash());
  assert(rview.hash() == rview_from_ptr_arr.hash());

  // Out-of-bounds safety and null pointer safety
  assert(rview_from_ptr_arr.source_type() == SQLITE_ROW_SOURCE_VIEW_PTR_ARRAY);
  assert(rview_from_ptr_arr.raw_view_ptr_array() == view_ptr_elems);
  assert(rview_from_ptr_arr[3].is_null());
  assert(rview_from_ptr_arr[-1].is_null());

  // Test SqliteRowView over array of view pointers (used for extracting
  // non-contiguous PKs from complete row)
  const SqliteValueView *ptr_with_null[2] = {&view_array_elems[0], nullptr};
  SqliteRowView rview_with_null_ptr(ptr_with_null, 2);
  assert(rview_with_null_ptr.size() == 2);
  assert(rview_with_null_ptr[0].as_int64() == 10);
  assert(rview_with_null_ptr[1].is_null());

  // Test SqliteRowOwnedView over contiguous array and pointer array
  SqliteRowOwnedView owned_view_from_arr(arr, 3);
  assert(owned_view_from_arr.size() == 3);
  assert(owned_view_from_arr.source_type() == SQLITE_ROW_OWNED_SOURCE_ARRAY);
  assert(owned_view_from_arr.raw_array() == arr);
  assert(owned_view_from_arr.as_int64(0) == 10);
  assert(owned_view_from_arr.as_text(1) == "alpha");
  assert(owned_view_from_arr.as_double(2) == 3.14);
  assert(owned_view_from_arr[0] == arr[0]);
  assert(owned_view_from_arr[1] == arr[1]);
  assert(owned_view_from_arr[2] == arr[2]);
  assert(owned_view_from_arr.hash() == wrap.hash());

  const SqliteValueOwned *owned_ptrs[3] = {&arr[0], &arr[1], &arr[2]};
  SqliteRowOwnedView owned_view_from_ptrs(owned_ptrs, 3);
  assert(owned_view_from_ptrs.size() == 3);
  assert(owned_view_from_ptrs.source_type() ==
         SQLITE_ROW_OWNED_SOURCE_PTR_ARRAY);
  assert(owned_view_from_ptrs.raw_ptr_array() == owned_ptrs);
  assert(owned_view_from_ptrs.as_int64(0) == 10);
  assert(owned_view_from_ptrs.as_text(1) == "alpha");
  assert(owned_view_from_ptrs.as_double(2) == 3.14);
  assert(owned_view_from_ptrs[0] == arr[0]);
  assert(owned_view_from_ptrs[1] == arr[1]);
  assert(owned_view_from_ptrs[2] == arr[2]);
  assert(owned_view_from_ptrs.hash() == wrap.hash());
  assert(owned_view_from_ptrs[3].is_null());
  assert(owned_view_from_ptrs[-1].is_null());

  // Relational comparisons across all representations
  assert_row_relops_equal(rview, owned_view_from_arr);
  assert_row_relops_equal(rview, owned_view_from_ptrs);
  assert_row_relops_equal(wrap, owned_view_from_arr);
  assert_row_relops_equal(wrap, owned_view_from_ptrs);
  assert_row_relops_equal(owned_view_from_arr, owned_view_from_ptrs);

  const SqliteValueOwned *owned_ptrs_with_null[2] = {&arr[0], nullptr};
  SqliteRowOwnedView owned_view_null_ptr(owned_ptrs_with_null, 2);
  assert(owned_view_null_ptr.size() == 2);
  assert(owned_view_null_ptr.source_type() ==
         SQLITE_ROW_OWNED_SOURCE_PTR_ARRAY);
  assert(owned_view_null_ptr.as_int64(0) == 10);
  assert(owned_view_null_ptr.is_null(1));
  assert(owned_view_null_ptr[1].is_null());

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
  const uint8_t blob_10_bytes[] = {'1', '0'};
  SqliteBlobView bv_10(blob_10_bytes, 2);
  SqliteValueOwned val_blob_10(bv_10);
  SqliteRowOwnedWrapper wrap_blob_10 =
      SqliteRowOwnedWrapper::create(val_blob_10);
  assert_row_relops_less(wrap_txt, wrap_blob_10);

  // ========================================================================
  // E. 1-Column Row Relational Operators across ALL Fundamental Types
  // ========================================================================
  sqlite3_stmt *stmt_int1 = nullptr;
  assert(sqlite3_prepare_v2(db, "SELECT 42;", -1, &stmt_int1, nullptr) ==
         SQLITE_OK);
  assert(sqlite3_step(stmt_int1) == SQLITE_ROW);
  SqliteRowView row_int1(stmt_int1);

  SqliteValueOwned val_42(42);
  SqliteRowOwnedWrapper wrap_int1 = SqliteRowOwnedWrapper::create(val_42);
  SqliteRowOwnedView oview_int1(val_42);

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

  assert_row_relops_equal(oview_int1, 42);
  assert_row_relops_equal(oview_int1, 42LL);
  assert_row_relops_equal(oview_int1, 42u);
  assert_row_relops_equal(oview_int1, 42UL);
  assert_row_relops_equal(oview_int1, 42ULL);
  assert(oview_int1 != 42.0);
  assert_row_relops_less(oview_int1, 50);

  // Direct comparison against SqliteValueOwned and SqliteValueView
  assert_row_relops_equal(row_int1, val_42);
  assert_row_relops_equal(wrap_int1, val_42);
  assert_row_relops_equal(oview_int1, val_42);
  assert_row_relops_equal(row_int1, row_int1[0]);
  assert_row_relops_equal(wrap_int1, row_int1[0]);
  assert_row_relops_equal(oview_int1, row_int1[0]);
  assert_row_relops_equal(oview_int1, wrap_int1);
  assert_row_relops_equal(oview_int1, row_int1);

  // Floating-point 1-column row
  sqlite3_stmt *stmt_dbl = nullptr;
  assert(sqlite3_prepare_v2(db, "SELECT 42.0;", -1, &stmt_dbl, nullptr) ==
         SQLITE_OK);
  assert(sqlite3_step(stmt_dbl) == SQLITE_ROW);
  SqliteRowView row_dbl(stmt_dbl);

  SqliteValueOwned val_42_dbl(42.0);
  SqliteRowOwnedWrapper wrap_dbl = SqliteRowOwnedWrapper::create(val_42_dbl);
  SqliteRowOwnedView oview_dbl(val_42_dbl);

  assert_row_relops_equal(row_dbl, 42.0);
  assert_row_relops_equal(row_dbl, 42.0f);
  assert(row_dbl != 42);
  assert_row_relops_less(row_dbl, 50.0);
  assert_row_relops_less(row_dbl, 50.0f);

  assert_row_relops_equal(wrap_dbl, 42.0);
  assert_row_relops_equal(wrap_dbl, 42.0f);
  assert(wrap_dbl != 42);
  assert_row_relops_less(wrap_dbl, 50.0);

  assert_row_relops_equal(oview_dbl, 42.0);
  assert_row_relops_equal(oview_dbl, 42.0f);
  assert(oview_dbl != 42);
  assert_row_relops_less(oview_dbl, 50.0);

  // Text 1-column row
  sqlite3_stmt *stmt_str = nullptr;
  assert(sqlite3_prepare_v2(db, "SELECT 'delta';", -1, &stmt_str, nullptr) ==
         SQLITE_OK);
  assert(sqlite3_step(stmt_str) == SQLITE_ROW);
  SqliteRowView row_str(stmt_str);

  SqliteValueOwned val_delta("delta");
  SqliteRowOwnedWrapper wrap_str = SqliteRowOwnedWrapper::create(val_delta);
  SqliteRowOwnedView oview_str(val_delta);

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

  assert_row_relops_equal(oview_str, "delta");
  assert_row_relops_equal(oview_str, SqliteStringView("delta"));
  assert_row_relops_equal(oview_str, str_owned);
  assert_row_relops_less(oview_str, "echo");

  // Boolean 1-column row
  sqlite3_stmt *stmt_bool = nullptr;
  assert(sqlite3_prepare_v2(db, "SELECT 1;", -1, &stmt_bool, nullptr) ==
         SQLITE_OK);
  assert(sqlite3_step(stmt_bool) == SQLITE_ROW);
  SqliteRowView row_bool(stmt_bool);
  SqliteValueOwned val_bool(true);
  SqliteRowOwnedView oview_bool(val_bool);
  assert_row_relops_equal(row_bool, true);
  assert_row_relops_equal(row_bool, 1);
  assert_row_relops_equal(oview_bool, true);
  assert_row_relops_equal(oview_bool, 1);

  // Blob 1-column row
  sqlite3_stmt *stmt_blob = nullptr;
  assert(sqlite3_prepare_v2(db, "SELECT X'01020304';", -1, &stmt_blob,
                            nullptr) == SQLITE_OK);
  assert(sqlite3_step(stmt_blob) == SQLITE_ROW);
  SqliteRowView row_blob(stmt_blob);
  const uint8_t blob_bytes[] = {0x01, 0x02, 0x03, 0x04};
  SqliteBlobView bv(blob_bytes, 4);
  assert_row_relops_equal(row_blob, bv);
  SqliteBlobOwned bo(bv.data(), bv.size());
  assert_row_relops_equal(row_blob, bo);

  SqliteValueOwned val_blob(bv);
  SqliteRowOwnedWrapper wrap_blob = SqliteRowOwnedWrapper::create(val_blob);
  SqliteRowOwnedView oview_blob(val_blob);
  assert_row_relops_equal(wrap_blob, bv);
  assert_row_relops_equal(wrap_blob, bo);
  assert_row_relops_equal(oview_blob, bv);
  assert_row_relops_equal(oview_blob, bo);

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
void test_row_iterators(sqlite3 *db) {
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
  for (const SqliteValueOwned &val : wrap) {
    w_sum += val.as_int64();
    w_count++;
  }
  assert(w_count == 4);
  assert(w_sum == 100);

  // 3. SqliteRowOwnedView Iterator (Contiguous and Pointer Array)
  SqliteRowOwnedView o_view(d_row, 4);
  sqlite3_int64 ov_sum = 0;
  int ov_count = 0;
  for (const SqliteValueOwned &val : o_view) {
    ov_sum += val.as_int64();
    ov_count++;
  }
  assert(ov_count == 4);
  assert(ov_sum == 100);

  const SqliteValueOwned *ptrs[4] = {&d_row[0], &d_row[1], &d_row[2], &d_row[3]};
  SqliteRowOwnedView o_ptr_view(ptrs, 4);
  sqlite3_int64 op_sum = 0;
  int op_count = 0;
  for (const SqliteValueOwned &val : o_ptr_view) {
    op_sum += val.as_int64();
    op_count++;
  }
  assert(op_count == 4);
  assert(op_sum == 100);

  // 4. Empty Row Iterator
  SqliteRowView empty_view;
  int e_count = 0;
  for (SqliteValueView col : empty_view) {
    (void)col;
    e_count++;
  }
  assert(e_count == 0);
  assert(empty_view.begin() == empty_view.end());

  SqliteRowOwnedView empty_owned_view;
  int eo_count = 0;
  for (const SqliteValueOwned &col : empty_owned_view) {
    (void)col;
    eo_count++;
  }
  assert(eo_count == 0);
  assert(empty_owned_view.begin() == empty_owned_view.end());
}

// ============================================================================
// 9. Exhaustive Test Suite for SqliteRowOwnedView
// ============================================================================
void test_row_owned_view_exhaustive(sqlite3 *db) {
  (void)db;
  printf("Testing SqliteRowOwnedView Exhaustive Suite...\n");

  // 1. Default empty view
  SqliteRowOwnedView empty_view;
  assert(empty_view.size() == 0);
  assert(empty_view.count() == 0);
  assert(empty_view.column_count() == 0);
  assert(empty_view.argc() == 0);
  assert(empty_view.empty());
  assert(empty_view.source_type() == SQLITE_ROW_OWNED_SOURCE_EMPTY);
  assert(!empty_view.is_ptr_array());
  assert(empty_view.raw_array() == nullptr);
  assert(empty_view.raw_ptr_array() == nullptr);
  assert(empty_view[0].is_null());
  assert(empty_view[-1].is_null());
  assert(empty_view.front().is_null());
  assert(empty_view.back().is_null());
  assert(empty_view.at(0).is_null());
  assert(empty_view.begin() == empty_view.end());
  assert(empty_view.cbegin() == empty_view.cend());
  assert(empty_view.rbegin() == empty_view.rend());

  // 2. Contiguous array creation
  SqliteValueOwned arr[4];
  arr[0] = SqliteValueOwned(100LL);
  arr[1] = SqliteValueOwned("Beta");
  arr[2] = SqliteValueOwned(3.14159);
  arr[3] = SqliteValueOwned(true);

  SqliteRowOwnedView arr_view(arr, 4);
  assert(arr_view.size() == 4);
  assert(!arr_view.empty());
  assert(arr_view.source_type() == SQLITE_ROW_OWNED_SOURCE_ARRAY);
  assert(!arr_view.is_ptr_array());
  assert(arr_view.raw_array() == arr);
  assert(arr_view.raw_ptr_array() == nullptr);
  assert(arr_view.front().as_int64() == 100);
  assert(arr_view.back().as_bool() == true);
  assert(arr_view[0].as_int64() == 100);
  assert(arr_view[1].as_text() == "Beta");
  assert(arr_view[2].as_double() == 3.14159);
  assert(arr_view[3].as_bool() == true);
  assert(arr_view.as_int64(0) == 100);
  assert(arr_view.as_text(1) == "Beta");
  assert(arr_view.as_double(2) == 3.14159);
  assert(arr_view.as_bool(3) == true);
  assert(arr_view.type(0) == SQLITE_INTEGER);
  assert(arr_view.type(1) == SQLITE_TEXT);
  assert(arr_view.type(2) == SQLITE_FLOAT);
  assert(arr_view.type(3) == SQLITE_INTEGER);

  // 3. Pointer array creation (Simulating Primary Key Extraction)
  // Suppose columns 0 (id=100) and 2 (score=3.14159) form a composite PK
  const SqliteValueOwned *pk_ptrs[2] = {&arr[0], &arr[2]};
  SqliteRowOwnedView pk_view(pk_ptrs, 2);
  assert(pk_view.size() == 2);
  assert(pk_view.source_type() == SQLITE_ROW_OWNED_SOURCE_PTR_ARRAY);
  assert(pk_view.is_ptr_array());
  assert(pk_view.raw_ptr_array() == pk_ptrs);
  assert(pk_view.raw_array() == nullptr);
  assert(pk_view[0].as_int64() == 100);
  assert(pk_view[1].as_double() == 3.14159);
  assert(pk_view.as_int64(0) == 100);
  assert(pk_view.as_double(1) == 3.14159);
  assert(pk_view.front().as_int64() == 100);
  assert(pk_view.back().as_double() == 3.14159);

  // Compare PK view against an exact compile-time stack tuple
  SqliteValueTuple<2> expected_pk_tuple(100LL, 3.14159);
  assert_row_relops_equal(pk_view, expected_pk_tuple);
  assert(pk_view.hash() == expected_pk_tuple.hash());

  // 4. Reverse iterators on SqliteRowOwnedView
  int rev_idx = 0;
  for (auto rit = arr_view.rbegin(); rit != arr_view.rend(); ++rit) {
    if (rev_idx == 0) assert(rit->as_bool() == true);
    if (rev_idx == 1) assert(rit->as_double() == 3.14159);
    if (rev_idx == 2) assert(rit->as_text() == "Beta");
    if (rev_idx == 3) assert(rit->as_int64() == 100);
    rev_idx++;
  }
  assert(rev_idx == 4);

  // Reverse iterator on pointer array
  int pk_rev_idx = 0;
  for (auto rit = pk_view.rbegin(); rit != pk_view.rend(); ++rit) {
    if (pk_rev_idx == 0) assert(rit->as_double() == 3.14159);
    if (pk_rev_idx == 1) assert(rit->as_int64() == 100);
    pk_rev_idx++;
  }
  assert(pk_rev_idx == 2);

  // 5. Conversions from Containers
  SqliteValueTuple<3> tuple3(10, 20, 30);
  SqliteRowOwnedView view_from_tuple(tuple3);
  assert(view_from_tuple.size() == 3);
  assert_row_relops_equal(view_from_tuple, tuple3);

  SqliteValueVec<4> vec4;
  vec4.push_back(10);
  vec4.push_back(20);
  vec4.push_back(30);
  SqliteRowOwnedView view_from_vec(vec4);
  assert(view_from_vec.size() == 3);
  assert_row_relops_equal(view_from_vec, vec4);
  assert_row_relops_equal(view_from_vec, view_from_tuple);

  // 6. Conversions from SqliteRowOwnedWrapper
  SqliteRowOwnedWrapper wrapper(arr, 4);
  SqliteRowOwnedView view_from_wrap = wrapper.to_view();
  assert(view_from_wrap.size() == 4);
  assert_row_relops_equal(view_from_wrap, wrapper);
  assert_row_relops_equal(view_from_wrap, arr_view);

  // 7. Single-value constructor
  SqliteValueOwned single_val("gamma");
  SqliteRowOwnedView single_view(single_val);
  assert(single_view.size() == 1);
  assert(single_view[0].as_text() == "gamma");
  assert_row_relops_equal(single_view, "gamma");
  assert_row_relops_equal(single_view, single_val);
  assert_row_relops_less(single_view, "zebra");

  // 8. Cross-container relational comparison with SqliteRowView
  sqlite3_stmt *stmt_cross = nullptr;
  assert(sqlite3_prepare_v2(db, "SELECT 100, 'Beta', 3.14159, 1;", -1, &stmt_cross, nullptr) == SQLITE_OK);
  assert(sqlite3_step(stmt_cross) == SQLITE_ROW);
  SqliteRowView row_view_instance(stmt_cross);

  assert_row_relops_equal(arr_view, row_view_instance);
  assert_row_relops_equal(row_view_instance, arr_view);
  sqlite3_finalize(stmt_cross);

  // 9. Lexicographical comparisons across various lengths & values
  SqliteValueTuple<2> tup_small(10, 20);
  SqliteValueTuple<2> tup_large(10, 30);
  SqliteValueTuple<3> tup_longer(10, 20, 5);
  SqliteRowOwnedView view_small(tup_small);
  SqliteRowOwnedView view_large(tup_large);
  SqliteRowOwnedView view_longer(tup_longer);

  assert_row_relops_less(view_small, view_large);
  assert_row_relops_less(view_small, tup_large);
  assert_row_relops_less(view_small, view_longer); // prefix is smaller than longer
  assert_row_relops_less(tup_small, view_longer);

  // 10. Transparent Functor checks (SqliteRowHash, SqliteRowEqual, SqliteRowLess)
  SqliteRowHash row_hash;
  SqliteRowEqual row_eq;
  SqliteRowLess row_less;

  assert(row_hash(view_from_tuple) == row_hash(tuple3));
  assert(row_hash(view_from_vec) == row_hash(view_from_tuple));
  assert(row_eq(view_from_tuple, tuple3));
  assert(row_eq(tuple3, view_from_tuple));
  assert(row_eq(view_from_tuple, view_from_vec));
  assert(row_less(view_small, view_large));
  assert(row_less(view_small, tup_large));
  assert(!row_less(view_large, view_small));
}

int main() {
  printf("================================================================\n");
  printf("RUNNING SQLITE ROW TESTS (SqliteRowView, SqliteRowOwnedWrapper)\n");
  printf("================================================================\n");

  sqlite3 *db = nullptr;
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
  test_row_owned_view_exhaustive(db);

  sqlite3_close(db);

  printf("================================================================\n");
  printf("ALL SQLITE ROW TESTS PASSED SUCCESSFULLY!\n");
  printf("================================================================\n");
  return 0;
}
