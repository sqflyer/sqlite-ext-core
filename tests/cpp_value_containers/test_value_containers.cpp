#define SQLITE_CORE
#include "sqlite3_statement.hpp"
#include "sqlite3_value_containers.hpp"
#include <assert.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// 1. SqliteValueTuple In-Situ Static Tests (N = 1..8)
// ============================================================================
static void test_value_tuple_static() {
  printf("1. Testing SqliteValueTuple<N> in-situ static memory (N=1..8)...\n");

  // Static footprint checks
  static_assert(sizeof(SqliteValueTuple<1>) == 16,
                "SqliteValueTuple<1> must be 16 bytes");
  static_assert(sizeof(SqliteValueTuple<2>) == 32,
                "SqliteValueTuple<2> must be 32 bytes");
  static_assert(sizeof(SqliteValueTuple<3>) == 48,
                "SqliteValueTuple<3> must be 48 bytes");
  static_assert(sizeof(SqliteValueTuple<4>) == 64,
                "SqliteValueTuple<4> must be 64 bytes");
  static_assert(sizeof(SqliteValueTuple<8>) == 128,
                "SqliteValueTuple<8> must be 128 bytes");

  // 1. Single Key Tuple (N = 1)
  SqliteValueTuple<1> t1;
  assert(t1.size() == 1);
  assert(t1.count() == 1);
  assert(!t1.empty());
  assert(t1.is_inline());
  assert(!t1.is_heap());
  assert(t1[0].is_null());

  t1[0] = SqliteValueOwned(100);
  assert(t1[0].is_integer());
  assert(t1[0].as_int() == 100);
  assert(t1.as_int(0) == 100);
  assert(t1.as_int() == 100);

  // Constructor from SqliteValueOwned
  SqliteValueTuple<1> t1_val(SqliteValueOwned(42));
  assert(t1_val[0].as_int() == 42);

  SqliteValueOwned src_val("hello");
  SqliteValueTuple<1> t1_str(src_val);
  assert(t1_str[0].is_text());
  assert(t1_str[0].as_text() == SqliteStringView("hello"));

  // 2. Composite Key Tuple (N = 2)
  SqliteValueTuple<2> t2;
  assert(t2.size() == 2);
  t2[0] = SqliteValueOwned(1001);
  t2[1] = SqliteValueOwned("device_A");

  assert(t2[0].as_int() == 1001);
  assert(t2[1].as_text() == SqliteStringView("device_A"));

  // Copy constructor & equality
  SqliteValueTuple<2> t2_copy(t2);
  assert(t2_copy == t2);
  assert(!(t2_copy != t2));
  assert(t2_copy.hash() == t2.hash());

  // Mutation does not affect copy
  t2_copy[1] = SqliteValueOwned("device_B");
  assert(t2_copy != t2);
  assert(t2[1].as_text() == SqliteStringView("device_A"));

  // Copy assignment & self-assignment
  SqliteValueTuple<2> t2_assign;
  t2_assign = t2;
  assert(t2_assign == t2);
  t2_assign = *&t2_assign;
  assert(t2_assign == t2);

  // Move constructor & move assignment
  SqliteValueTuple<2> t2_move(sqlite_move(t2_assign));
  assert(t2_move == t2);

  // 3. Wide Tuples (N = 4, N = 8)
  SqliteValueTuple<4> t4;
  assert(t4.size() == 4);
  for (int i = 0; i < 4; ++i) {
    t4[i] = SqliteValueOwned(i * 10);
  }
  for (int i = 0; i < 4; ++i) {
    assert(t4[i].as_int() == i * 10);
  }

  SqliteValueTuple<8> t8;
  assert(t8.size() == 8);
  for (int i = 0; i < 8; ++i) {
    t8[i] = SqliteValueOwned(i * 100);
  }
  for (int i = 0; i < 8; ++i) {
    assert(t8[i].as_int() == i * 100);
  }

  // Range-based for loop iteration
  int iter_count = 0;
  for (const SqliteValueOwned &v : t4) {
    assert(v.as_int() == iter_count * 10);
    iter_count++;
  }
  assert(iter_count == 4);

  // View conversion to SqliteRowOwnedWrapper
  SqliteRowOwnedWrapper span = t4.view();
  assert(span.size() == 4);
  assert(span[0].as_int() == 0);
  // Test set_null_all()
  t4.set_null_all();
  for (int i = 0; i < 4; ++i) {
    assert(t4[i].is_null());
  }

  // Out-of-bounds safety
  assert(t4[99].is_null());
  assert(t4[-5].is_null());

  printf("   [PASS] SqliteValueTuple<N> static verified.\n");
}

// ============================================================================
// 2. SqliteValueTuple Heap Fallback Tests (N == 0 / default SqliteValueTuple<>)
// ============================================================================
static void test_value_tuple_heap() {
  printf("2. Testing SqliteValueTuple<> / SqliteValueTuple<0> direct heap tuple...\n");

  SqliteValueTuple<> t9(9);
  assert(t9.size() == 9);
  assert(!t9.empty());
  assert(!t9.is_inline());
  assert(t9.is_heap());

  for (int i = 0; i < 9; ++i) {
    t9[i] = SqliteValueOwned(i + 1);
  }
  for (int i = 0; i < 9; ++i) {
    assert(t9[i].as_int() == i + 1);
  }

  // Copy & Move semantics for heap tuples
  SqliteValueTuple<0> t9_copy(t9);
  assert(t9_copy == t9);
  assert(t9_copy.hash() == t9.hash());

  t9_copy[0] = SqliteValueOwned(999);
  assert(t9_copy != t9);
  assert(t9[0].as_int() == 1);

  SqliteValueTuple<> t9_move(sqlite_move(t9_copy));
  assert(t9_move[0].as_int() == 999);

  SqliteValueTuple<> t16(16);
  assert(t16.size() == 16);
  t16[15] = SqliteValueOwned("last_col");
  SqliteRowOwnedWrapper span16 = t16.view();
  assert(span16.size() == 16);
  assert(span16[15].as_text() == SqliteStringView("last_col"));

  // Single value and projecting constructors for heap tuples
  SqliteValueTuple<> t9_single(SqliteValueOwned(777));
  assert(t9_single.size() == 1);
  assert(t9_single[0].as_int() == 777);

  SqliteValueTuple<> t9_projected(t9, nullptr, 9);
  assert(t9_projected == t9);

  printf("   [PASS] SqliteValueTuple<0> heap verified.\n");
}

// ============================================================================
// 3. SqliteValueVec Adaptive Stack SBO & Spilling Tests (N = 1..8)
// ============================================================================
static void test_value_vec_adaptive_sbo() {
  printf(
      "3. Testing SqliteValueVec<N> adaptive stack SBO & heap spilling...\n");

  static_assert(sizeof(SqliteValueVec<1>) == 16,
                "SqliteValueVec<1> must be 16 bytes");
  static_assert(sizeof(SqliteValueVec<2>) == 32,
                "SqliteValueVec<2> must be 32 bytes");
  static_assert(sizeof(SqliteValueVec<4>) == 64,
                "SqliteValueVec<4> must be 64 bytes");
  static_assert(sizeof(SqliteValueVec<8>) == 128,
                "SqliteValueVec<8> must be 128 bytes");

  // 1. In-Situ Stack Usage (N = 4)
  SqliteValueVec<4> vec;
  assert(vec.empty());
  assert(vec.size() == 0);
  assert(vec.is_inline());

  vec.resize(3);
  assert(vec.size() == 3);
  assert(vec.is_inline());

  vec[0] = SqliteValueOwned(10);
  vec[1] = SqliteValueOwned(20);
  vec[2] = SqliteValueOwned("test");

  assert(vec[0].as_int() == 10);
  assert(vec[1].as_int() == 20);
  assert(vec[2].as_text() == SqliteStringView("test"));
  assert(vec.as_int(0) == 10);
  assert(vec.as_int(1) == 20);
  assert(vec.as_text(2) == SqliteStringView("test"));

  // 2. Growing within In-Situ capacity (3 -> 4)
  vec.resize(4);
  assert(vec.size() == 4);
  assert(vec.is_inline());
  vec[3] = SqliteValueOwned(3.14);
  assert(vec[0].as_int() == 10);
  assert(vec[3].as_double() > 3.13);

  // 3. Spilling to Heap (4 -> 6)
  vec.resize(6);
  assert(vec.size() == 6);
  assert(!vec.is_inline()); // Now on heap!
  assert(vec[0].as_int() == 10);
  assert(vec[1].as_int() == 20);
  assert(vec[2].as_text() == SqliteStringView("test"));
  assert(vec[3].as_double() > 3.13);

  vec[4] = SqliteValueOwned(40);
  vec[5] = SqliteValueOwned(50);
  assert(vec[4].as_int() == 40);
  assert(vec[5].as_int() == 50);

  // 4. Shrinking back from Heap to Stack (6 -> 2)
  vec.resize(2);
  assert(vec.size() == 2);
  assert(vec.is_inline()); // Returned to in-situ stack!
  assert(vec[0].as_int() == 10);
  assert(vec[1].as_int() == 20);

  // Test set_null_all()
  vec.set_null_all();
  assert(vec[0].is_null());
  assert(vec[1].is_null());

  // 5. Shrinking to 0
  vec.resize(0);
  assert(vec.empty());
  assert(vec.size() == 0);

  // 6. Copy construction across stack and heap modes
  SqliteValueVec<4> v_stack;
  v_stack.resize(2);
  v_stack[0] = SqliteValueOwned(1);
  v_stack[1] = SqliteValueOwned(2);

  SqliteValueVec<4> v_stack_copy(v_stack);
  assert(v_stack_copy.is_inline());
  assert(v_stack_copy == v_stack);

  SqliteValueVec<4> v_heap;
  v_heap.resize(8);
  for (int i = 0; i < 8; ++i)
    v_heap[i] = SqliteValueOwned(i * 11);

  SqliteValueVec<4> v_heap_copy(v_heap);
  assert(!v_heap_copy.is_inline());
  assert(v_heap_copy == v_heap);
  assert(v_heap_copy.hash() == v_heap.hash());

  // Copy assignment from heap to stack
  v_stack = v_heap;
  assert(!v_stack.is_inline());
  assert(v_stack == v_heap);

  // Move assignment
  SqliteValueVec<4> v_moved;
  v_moved = sqlite_move(v_stack);
  assert(v_moved.size() == 8);
  assert(v_stack.empty());

  // Hash calculation stability
  assert(v_moved.hash() != 0);

  // Range-based for loop iteration
  int v_count = 0;
  for (const SqliteValueOwned &val : v_moved) {
    assert(val.as_int() == v_count * 11);
    v_count++;
  }
  assert(v_count == 8);

  // 7. Dynamic Vector Methods: push_back, emplace_back, pop_back, reserve,
  // clear
  SqliteValueVec<2> dyn_vec;
  assert(dyn_vec.empty());
  assert(dyn_vec.capacity() == 2);
  assert(dyn_vec.is_inline());

  dyn_vec.push_back(100);
  assert(dyn_vec.size() == 1);
  assert(dyn_vec.is_inline());
  assert(dyn_vec[0] == 100);

  dyn_vec.push_back(200);
  assert(dyn_vec.size() == 2);
  assert(dyn_vec.is_inline());
  assert(dyn_vec[1] == 200);

  // Overflow SBO stack capacity (2 -> spills to heap)
  dyn_vec.push_back(300);
  assert(dyn_vec.size() == 3);
  assert(!dyn_vec.is_inline());
  assert(dyn_vec[0] == 100);
  assert(dyn_vec[1] == 200);
  assert(dyn_vec[2] == 300);

  dyn_vec.emplace_back("pushed_string");
  assert(dyn_vec.size() == 4);
  assert(dyn_vec[3] == "pushed_string");

  dyn_vec.pop_back();
  assert(dyn_vec.size() == 3);
  assert(dyn_vec[2] == 300);

  dyn_vec.reserve(32);
  assert(dyn_vec.capacity() >= 32);
  assert(dyn_vec.size() == 3);

  dyn_vec.clear();
  assert(dyn_vec.empty());
  assert(dyn_vec.size() == 0);

  printf("   [PASS] SqliteValueVec<N> adaptive SBO verified.\n");
}

// ============================================================================
// 4. SqliteValueVec Direct Heap Vector Tests (N == 0 / default SqliteValueVec<>)
// ============================================================================
static void test_value_vec_heap() {
  printf("4. Testing SqliteValueVec<> / SqliteValueVec<0> direct heap vector...\n");

  SqliteValueVec<> vec(12);
  assert(vec.size() == 12);
  assert(!vec.is_inline());

  for (int i = 0; i < 12; ++i) {
    vec[i] = SqliteValueOwned(i * 5);
  }
  for (int i = 0; i < 12; ++i) {
    assert(vec[i].as_int() == i * 5);
  }

  vec.resize(15);
  assert(vec.size() == 15);
  assert(vec[0].as_int() == 0);
  assert(vec[11].as_int() == 55);

  vec.resize(5);
  assert(vec.size() == 5);
  assert(vec[4].as_int() == 20);

  SqliteValueVec<0> vec_copy(vec);
  assert(vec_copy == vec);

  SqliteValueVec<> vec_moved(sqlite_move(vec));
  assert(vec_moved.size() == 5);
  assert(vec.empty());

  // Dynamic methods on heap vector (default constructed empty)
  SqliteValueVec<> h_vec;
  assert(h_vec.empty());
  assert(h_vec.size() == 0);
  assert(h_vec.capacity() == 0);

  h_vec.push_back(555);
  assert(h_vec.size() == 1);
  assert(h_vec[0] == 555);

  h_vec.emplace_back("heap_string");
  assert(h_vec.size() == 2);
  assert(h_vec[1] == "heap_string");

  h_vec.pop_back();
  assert(h_vec.size() == 1);
  assert(h_vec[0] == 555);

  h_vec.reserve(64);
  assert(h_vec.capacity() >= 64);
  assert(h_vec.size() == 1);

  h_vec.clear();
  assert(h_vec.empty());
  assert(h_vec.size() == 0);

  printf("   [PASS] SqliteValueVec<0> direct heap verified.\n");
}

// ============================================================================
// 5. Scope Dispatcher Integration Tests (withSqliteRowOwned)
// ============================================================================
static void test_containers_dispatch() {
  printf("5. Testing withSqliteRowOwned exhaustive branch coverage (-100..64 "
         "cols)...\n");

  // 1. Test Edge Cases: negative and zero sizes
  int zero_size_len = -1;
  bool zero_is_null_ptr = false;
  withSqliteRowOwned(0, [&](SqliteRowOwnedWrapper row) {
    zero_size_len = row.size();
    zero_is_null_ptr = (row.data() == nullptr);
    assert(row.empty());
  });
  assert(zero_size_len == 0);
  assert(zero_is_null_ptr);

  withSqliteRowOwned(-1, [&](SqliteRowOwnedWrapper row) {
    assert(row.size() == 0);
    assert(row.data() == nullptr);
  });

  withSqliteRowOwned(-100, [&](SqliteRowOwnedWrapper row) {
    assert(row.size() == 0);
    assert(row.data() == nullptr);
  });

  // 2. Exhaustive branch coverage for all stack specializations (case 1..8)
  for (int cols = 1; cols <= 8; ++cols) {
    int observed_size = 0;
    withSqliteRowOwned(cols, [&](SqliteRowOwnedWrapper row) {
      observed_size = row.size();
      assert(row.size() == cols);
      assert(!row.empty());
      assert(row.data() != nullptr);

      // All slots must start as canonical SQLITE_NULL
      for (int i = 0; i < cols; ++i) {
        assert(row[i].is_null());
      }

      // Populate all slots with distinct typed values
      for (int i = 0; i < cols; ++i) {
        if (i % 3 == 0) {
          row[i] = SqliteValueOwned(i * 100 + 1);
        } else if (i % 3 == 1) {
          row[i] = SqliteValueOwned(3.14 * (i + 1));
        } else {
          row[i] = SqliteValueOwned("col_val");
        }
      }

      // Verify populated elements
      for (int i = 0; i < cols; ++i) {
        if (i % 3 == 0) {
          assert(row[i].as_int() == i * 100 + 1);
        } else if (i % 3 == 1) {
          assert(row[i].as_double() > 0.0);
        } else {
          assert(row[i].as_text() == SqliteStringView("col_val"));
        }
      }

      // Verify range-based for loop iteration over wrapper
      int loop_count = 0;
      for (const auto &val : row) {
        (void)val;
        ++loop_count;
      }
      assert(loop_count == cols);
    });
    assert(observed_size == cols);
  }

  // 3. Exhaustive branch coverage for default case (heap fallback for size >=
  // 9)
  const int heap_sizes[] = {9, 10, 11, 12, 16, 25, 32, 64};
  for (size_t s = 0; s < sizeof(heap_sizes) / sizeof(heap_sizes[0]); ++s) {
    int target_cols = heap_sizes[s];
    int observed_size = 0;
    withSqliteRowOwned(target_cols, [&](SqliteRowOwnedWrapper row) {
      observed_size = row.size();
      assert(row.size() == target_cols);
      assert(row.data() != nullptr);

      // First and last slot must start as canonical SQLITE_NULL
      assert(row[0].is_null());
      assert(row[target_cols - 1].is_null());

      // Write first, middle, and last slots
      row[0] = SqliteValueOwned(1001);
      row[target_cols / 2] = SqliteValueOwned("mid_element");
      row[target_cols - 1] = SqliteValueOwned(99.99);

      assert(row[0].as_int() == 1001);
      assert(row[target_cols / 2].as_text() == SqliteStringView("mid_element"));
      assert(row[target_cols - 1].as_double() > 99.0);
    });
    assert(observed_size == target_cols);
  }

  // 4. Return Value Forwarding across primitive and composite types
  int ret_int = withSqliteRowOwned(4, [](SqliteRowOwnedWrapper row) -> int {
    row[0] = SqliteValueOwned(555);
    return row[0].as_int();
  });
  assert(ret_int == 555);

  double ret_dbl =
      withSqliteRowOwned(2, [](SqliteRowOwnedWrapper row) -> double {
        row[1] = SqliteValueOwned(2.71828);
        return row[1].as_double();
      });
  assert(ret_dbl > 2.71 && ret_dbl < 2.72);

  const char *ret_str = withSqliteRowOwned(
      1, [](SqliteRowOwnedWrapper) -> const char * { return "success"; });
  assert(strcmp(ret_str, "success") == 0);

  // 5. Relational Comparisons and Hashing inside withSqliteRowOwned scope
  withSqliteRowOwned(3, [](SqliteRowOwnedWrapper row1) {
    row1[0] = SqliteValueOwned(10);
    row1[1] = SqliteValueOwned("same");
    row1[2] = SqliteValueOwned(3.0);

    withSqliteRowOwned(3, [&](SqliteRowOwnedWrapper row2) {
      row2[0] = SqliteValueOwned(10);
      row2[1] = SqliteValueOwned("same");
      row2[2] = SqliteValueOwned(3.0);

      assert(row1 == row2);
      assert(row1.hash() == row2.hash());

      row2[2] = SqliteValueOwned(4.0);
      assert(row1 < row2);
      assert(row1 != row2);
    });
  });

  printf("   [PASS] withSqliteRowOwned complete coverage verified (100%% "
         "branches).\n");
}

// ============================================================================
// 6. SQLite Statement Binding & Result Verification
// ============================================================================
static void test_containers_sqlite_sql() {
  printf("6. Testing SQLite SQL binding and row reflection...\n");

  sqlite3 *db = nullptr;
  assert(sqlite3_open(":memory:", &db) == SQLITE_OK);

  sqlite3_stmt *stmt = nullptr;
  assert(sqlite3_prepare_v2(db, "SELECT ?1, ?2, ?3;", -1, &stmt, nullptr) ==
         SQLITE_OK);

  SqliteValueTuple<3> tuple;
  tuple[0] = SqliteValueOwned(12345);
  tuple[1] = SqliteValueOwned("sql_test");
  tuple[2] = SqliteValueOwned(99.5);

  tuple[0].bind(stmt, 1);
  tuple[1].bind(stmt, 2);
  tuple[2].bind(stmt, 3);

  assert(sqlite3_step(stmt) == SQLITE_ROW);
  assert(sqlite3_column_int(stmt, 0) == 12345);
  assert(strcmp((const char *)sqlite3_column_text(stmt, 1), "sql_test") == 0);
  assert(sqlite3_column_double(stmt, 2) > 99.0);

  // Read back into SqliteValueVec<4>
  SqliteRowView rview(stmt);
  assert(rview.size() == 3);

  SqliteValueVec<4> row_vec;
  row_vec.resize(rview.size());
  for (int i = 0; i < rview.size(); ++i) {
    row_vec[i] = rview[i].to_owned();
  }
  assert(row_vec[0].as_int() == 12345);
  assert(row_vec[1].as_text() == SqliteStringView("sql_test"));

  sqlite3_finalize(stmt);
  sqlite3_close(db);

  printf("   [PASS] SQLite SQL binding and row reflection verified.\n");
}

// ============================================================================
// 7. Generic 8x8 Compile-Time Matrix Dispatch Framework Tests
// ============================================================================

struct IMockTable {
  virtual ~IMockTable() {}
  virtual int get_tag() const = 0;
  virtual size_t get_key_size() const = 0;
  virtual size_t get_val_size() const = 0;
};

template <typename KeyContainer, typename ValContainer>
struct MockStorageTable : public IMockTable {
  KeyContainer key;
  ValContainer val;
  int tag;

  MockStorageTable(int t) : tag(t) {}
  virtual int get_tag() const override { return tag; }
  virtual size_t get_key_size() const override { return sizeof(KeyContainer); }
  virtual size_t get_val_size() const override { return sizeof(ValContainer); }
};

struct IMockScratchTable {
  virtual ~IMockScratchTable() {}
  virtual int get_tag() const = 0;
  virtual size_t get_val_size() const = 0;
};

template <typename ValContainer>
struct MockScratchTable : public IMockScratchTable {
  ValContainer val;
  int tag;

  MockScratchTable(int t) : tag(t) {}
  virtual int get_tag() const override { return tag; }
  virtual size_t get_val_size() const override { return sizeof(ValContainer); }
};

static IMockScratchTable *create_scratch_1d_tuple(int count, int tag) {
  SQLITE_MAKE_DEFAULT_STORAGE_1D_8(MockScratchTable, count, tag);
}

static IMockScratchTable *create_scratch_1d_vec(int count, int tag) {
  SQLITE_MAKE_DEFAULT_VEC_STORAGE_1D_8(MockScratchTable, count, tag);
}

static IMockTable *create_storage_8x8_default(int pk, int val, int tag) {
  SQLITE_MAKE_DEFAULT_STORAGE_8X8(MockStorageTable, pk, val, tag);
}

static IMockTable *create_storage_8x8_tuple(int pk, int val, int tag) {
  SQLITE_MAKE_DEFAULT_TUPLE_STORAGE_8X8(MockStorageTable, pk, val, tag);
}

static IMockTable *create_storage_8x8_vec(int pk, int val, int tag) {
  SQLITE_MAKE_DEFAULT_VEC_STORAGE_8X8(MockStorageTable, pk, val, tag);
}

static IMockTable *create_storage_8x8_custom(int pk, int val, int tag) {
  SQLITE_MAKE_STORAGE_8X8(MockStorageTable, SqliteValueTuple, SqliteValueTuple, pk, val, tag);
}

static void test_dispatch_framework() {
  printf("7. Testing Generic 8x8 compile-time dispatch framework...\n");

  // 1. 1D Dispatch test across boundary and active ranges (-5..12)
  for (int cols = -5; cols <= 12; ++cols) {
    size_t dispatched_1d_n = 999;
    SQLITE_DISPATCH_1D_8(ColsN, cols, {
      MockScratchTable<SqliteValueTuple<ColsN>> table(cols);
      dispatched_1d_n = ColsN;
      assert(table.tag == cols);
    });
    if (cols >= 1 && cols <= 8) {
      assert(dispatched_1d_n == static_cast<size_t>(cols));
    } else {
      assert(dispatched_1d_n == 0); // Out of 1..8 range maps to N=0 (<> default heap)
    }
  }

  // 2. 2D Dispatch test across -2..10 x -2..10 matrix
  for (int k = -2; k <= 10; ++k) {
    for (int v = -2; v <= 10; ++v) {
      size_t observed_k = 999;
      size_t observed_v = 999;
      SQLITE_DISPATCH_2D_8X8(KeyN, ValN, k, v, {
        observed_k = KeyN;
        observed_v = ValN;
      });
      assert(observed_k == (k >= 1 && k <= 8 ? static_cast<size_t>(k) : 0));
      assert(observed_v == (v >= 1 && v <= 8 ? static_cast<size_t>(v) : 0));
    }
  }

  // 3. 1D Factory Macro Verification (SQLITE_MAKE_DEFAULT_STORAGE_1D_8 & SQLITE_MAKE_DEFAULT_VEC_STORAGE_1D_8)
  for (int cols = 0; cols <= 10; ++cols) {
    IMockScratchTable *t_tup = create_scratch_1d_tuple(cols, 100 + cols);
    assert(t_tup != nullptr);
    assert(t_tup->get_tag() == 100 + cols);
    if (cols >= 1 && cols <= 8) {
      assert(t_tup->get_val_size() == static_cast<size_t>(cols) * 16);
    } else {
      assert(t_tup->get_val_size() == 16); // SqliteValueTuple<0> is 16 bytes
    }
    sqlite_delete(t_tup);

    IMockScratchTable *t_vec = create_scratch_1d_vec(cols, 200 + cols);
    assert(t_vec != nullptr);
    assert(t_vec->get_tag() == 200 + cols);
    if (cols >= 1 && cols <= 8) {
      assert(t_vec->get_val_size() == static_cast<size_t>(cols) * 16);
    } else {
      assert(t_vec->get_val_size() == 16); // SqliteValueVec<0> is 16 bytes
    }
    sqlite_delete(t_vec);
  }

  // 4. 2D Factory Macro Verification (SQLITE_MAKE_DEFAULT_STORAGE_8X8 & SQLITE_MAKE_STORAGE_8X8)
  for (int k = 0; k <= 9; ++k) {
    for (int v = 0; v <= 9; ++v) {
      IMockTable *t_8x8 = create_storage_8x8_default(k, v, k * 100 + v);
      assert(t_8x8 != nullptr);
      assert(t_8x8->get_tag() == k * 100 + v);
      if (k >= 1 && k <= 8) {
        assert(t_8x8->get_key_size() == static_cast<size_t>(k) * 16);
      } else {
        assert(t_8x8->get_key_size() == 16); // SqliteValueTuple<0>
      }
      if (v >= 1 && v <= 8) {
        assert(t_8x8->get_val_size() == static_cast<size_t>(v) * 16);
      } else {
        assert(t_8x8->get_val_size() == 16); // SqliteValueVec<0>
      }
      sqlite_delete(t_8x8);

      IMockTable *t_tup_tup = create_storage_8x8_tuple(k, v, k * 100 + v);
      assert(t_tup_tup != nullptr);
      assert(t_tup_tup->get_tag() == k * 100 + v);
      sqlite_delete(t_tup_tup);

      IMockTable *t_vec_vec = create_storage_8x8_vec(k, v, k * 100 + v);
      assert(t_vec_vec != nullptr);
      assert(t_vec_vec->get_tag() == k * 100 + v);
      sqlite_delete(t_vec_vec);

      IMockTable *t_cust = create_storage_8x8_custom(k, v, 5000 + k * 10 + v);
      assert(t_cust != nullptr);
      assert(t_cust->get_tag() == 5000 + k * 10 + v);
      sqlite_delete(t_cust);
    }
  }

  // 5. Direct SqliteRowOwnedWrapper Scope Dispatch Macros (1D & 2D)
  for (int cols = 0; cols <= 10; ++cols) {
    bool executed = false;
    SQLITE_WITH_ROW_OWNED_1D(row, cols, {
      executed = true;
      assert(row.size() == cols);
      for (int i = 0; i < cols; ++i) {
        row[i] = SqliteValueOwned(i * 10);
        assert(row[i].as_int() == i * 10);
      }
    });
    assert(executed);

    bool vec_executed = false;
    SQLITE_WITH_VEC_ROW_1D(vec_row, cols, {
      vec_executed = true;
      assert(vec_row.size() == cols);
      for (int i = 0; i < cols; ++i) {
        vec_row[i] = SqliteValueOwned(i * 20);
        assert(vec_row[i].as_int() == i * 20);
      }
    });
    assert(vec_executed);
  }

  // 6. Direct 2D SqliteRowOwnedWrapper Scope Dispatch & Functional Dispatcher
  for (int k = 0; k <= 9; ++k) {
    for (int v = 0; v <= 9; ++v) {
      bool executed_2d = false;
      SQLITE_WITH_KEY_VAL_OWNED_8X8(key_row, val_row, k, v, {
        executed_2d = true;
        assert(key_row.size() == k);
        assert(val_row.size() == v);
        for (int i = 0; i < k; ++i) key_row[i] = SqliteValueOwned(100 + i);
        for (int i = 0; i < v; ++i) val_row[i] = SqliteValueOwned(200 + i);
      });
      assert(executed_2d);

      bool executed_fn = false;
      int ret = withSqliteKeyValOwned(k, v, [&](SqliteRowOwnedWrapper key_span, SqliteRowOwnedWrapper val_span) {
        executed_fn = true;
        assert(key_span.size() == k);
        assert(val_span.size() == v);
        return k * 1000 + v;
      });
      assert(executed_fn);
      assert(ret == k * 1000 + v);
    }
  }

  printf("   [PASS] Generic 8x8 dispatch framework verified.\n");
}

// ============================================================================
// 8. Static Null Template & Single-Burst SIMD Initialization Tests
// ============================================================================
static void test_null_mechanics_and_simd_init() {
  printf("8. Testing Static Null Template & Single-Burst SIMD "
         "Initialization...\n");

  // 1. Verify SqliteValueOwned::static_null() invariants
  const SqliteValueOwned &s_null = SqliteValueOwned::static_null();
  assert(s_null.is_null());
  assert(s_null.is_active());
  assert(s_null.type() == SQLITE_NULL);
  assert(!s_null.is_heap_allocated());
  assert(s_null.subtype() == SQLITE_SUBTYPE_NONE);
  assert(sizeof(s_null) == 16);

  // 2. Verify SqliteValueOwned::static_null_array() invariants (8 contiguous
  // nulls = 128 bytes)
  const SqliteValueOwned *null_arr = SqliteValueOwned::static_null_array();
  assert(null_arr != nullptr);
  for (int i = 0; i < 8; ++i) {
    assert(null_arr[i].is_null());
    assert(null_arr[i].is_active());
    assert(null_arr[i].type() == SQLITE_NULL);
    assert(!null_arr[i].is_heap_allocated());
  }

  // 3. Test SqliteValueOwned::set_null() with SBO, scalar, and heap memory
  // release
  SqliteValueOwned val_int(42);
  assert(!val_int.is_null());
  val_int.set_null();
  assert(val_int.is_null());
  assert(val_int.is_active());

  SqliteValueOwned val_sbo("inline_sbo");
  assert(val_sbo.is_text());
  val_sbo.set_null();
  assert(val_sbo.is_null());

  SqliteValueOwned val_heap("A very long heap string that allocates a buffer "
                            "and exceeds 13 characters");
  assert(val_heap.is_heap_allocated());
  val_heap.set_null(); // Safely frees heap memory and sets tag = 0xA0
  assert(val_heap.is_null());
  assert(!val_heap.is_heap_allocated());

  // 4. Test Single-Burst SIMD init_null_values() across all tuple widths (N
  // = 1..8)
  SqliteValueTuple<1> t1;
  assert(t1[0].is_null());
  assert(t1[0].is_active());

  SqliteValueTuple<2> t2;
  assert(t2[0].is_null() && t2[1].is_null());

  SqliteValueTuple<3> t3;
  assert(t3[0].is_null() && t3[1].is_null() && t3[2].is_null());

  SqliteValueTuple<4> t4;
  for (int i = 0; i < 4; ++i) {
    assert(t4[i].is_null());
    assert(t4[i].is_active());
  }

  SqliteValueTuple<8> t8;
  for (int i = 0; i < 8; ++i) {
    assert(t8[i].is_null());
    assert(t8[i].is_active());
  }

  // 5. Test Partial Constructors preserving default NULLs in trailing slots
  SqliteValueTuple<4> t4_single(SqliteValueOwned(999));
  assert(t4_single[0].as_int() == 999);
  assert(t4_single[1].is_null());
  assert(t4_single[2].is_null());
  assert(t4_single[3].is_null());

  SqliteValueTuple<2> t2_source;
  t2_source[0] = SqliteValueOwned(10);
  t2_source[1] = SqliteValueOwned(20);
  SqliteValueTuple<4> t4_projected(t2_source, nullptr, 2);
  assert(t4_projected[0].as_int() == 10);
  assert(t4_projected[1].as_int() == 20);
  assert(t4_projected[2].is_null());
  assert(t4_projected[3].is_null());

  // 6. Test Individual Slot set_null() on Tuples without affecting sibling
  // slots
  SqliteValueTuple<3> t3_hetero;
  t3_hetero[0] = SqliteValueOwned(100);
  t3_hetero[1] = SqliteValueOwned("tenant_A");
  t3_hetero[2] = SqliteValueOwned(99.5);
  assert(!t3_hetero[0].is_null() && !t3_hetero[1].is_null() &&
         !t3_hetero[2].is_null());

  t3_hetero[1].set_null(); // Only slot 1 reset to NULL
  assert(t3_hetero[0].as_int() == 100);
  assert(t3_hetero[1].is_null());
  assert(t3_hetero[2].as_double() == 99.5);

  // 7. Test Default Tuple Equality and Hash Invariants
  SqliteValueTuple<4> t4_null_a;
  SqliteValueTuple<4> t4_null_b;
  assert(t4_null_a == t4_null_b);
  assert(!(t4_null_a != t4_null_b));
  assert(t4_null_a.hash() == t4_null_b.hash());

  // 8. Test set_null_all() on Tuples (N=1..8 and N>=9)
  for (int i = 0; i < 4; ++i)
    t4[i] = SqliteValueOwned(i * 10);
  assert(!t4[0].is_null() && !t4[3].is_null());
  t4.set_null_all();
  for (int i = 0; i < 4; ++i)
    assert(t4[i].is_null());
  assert(t4 == t4_null_a);

  for (int i = 0; i < 8; ++i)
    t8[i] = SqliteValueOwned("device");
  assert(!t8[0].is_null() && !t8[7].is_null());
  t8.set_null_all();
  for (int i = 0; i < 8; ++i)
    assert(t8[i].is_null());

  SqliteValueTuple<> t9(9);
  for (int i = 0; i < 9; ++i)
    t9[i] = SqliteValueOwned(i + 1);
  assert(!t9[0].is_null());
  t9.set_null_all();
  for (int i = 0; i < 9; ++i)
    assert(t9[i].is_null());

  // 9. Test Wide Heap Tuple (N=16) SIMD Chunked Initialization & Reset
  SqliteValueTuple<> t16(16);
  assert(t16.size() == 16);
  assert(t16.is_heap());
  for (int i = 0; i < 16; ++i) {
    assert(t16[i].is_null());
    assert(t16[i].is_active());
    t16[i] = SqliteValueOwned(i * 100);
  }
  assert(t16[0].as_int() == 0);
  assert(t16[15].as_int() == 1500);
  t16.set_null_all();
  for (int i = 0; i < 16; ++i) {
    assert(t16[i].is_null());
  }

  // 10. Test set_null_all() on Vectors
  SqliteValueVec<4> vec(3);
  vec[0] = SqliteValueOwned(100);
  vec[1] = SqliteValueOwned("vector");
  vec[2] = SqliteValueOwned(3.14);
  assert(!vec[0].is_null());
  vec.set_null_all();
  assert(vec.size() == 3);
  assert(vec[0].is_null());
  assert(vec[1].is_null());
  assert(vec[2].is_null());

  // 11. Test Initializer List & Array Constructors for Tuples and Vectors
  SqliteValueTuple<3> t_init_owned = {
      SqliteValueOwned(10), SqliteValueOwned("alpha"), SqliteValueOwned(3.14)};
  assert(t_init_owned[0].as_int() == 10);
  assert(t_init_owned[1].as_text() == SqliteStringView("alpha"));
  assert(t_init_owned[2].as_double() > 3.13);

  SqliteValueOwned raw_c_arr[2] = {SqliteValueOwned(100),
                                   SqliteValueOwned(200)};
  SqliteValueTuple<4> t_c_arr(raw_c_arr);
  assert(t_c_arr[0].as_int() == 100);
  assert(t_c_arr[1].as_int() == 200);
  assert(t_c_arr[2].is_null());
  assert(t_c_arr[3].is_null());

  // 12. Test Generic Primitive Initializer Lists & Native C-Arrays (int,
  // double, const char*)
  SqliteValueTuple<3> t_ints = {101, 102, 103};
  assert(t_ints[0].as_int() == 101);
  assert(t_ints[1].as_int() == 102);
  assert(t_ints[2].as_int() == 103);

  SqliteValueTuple<3> t_doubles = {1.25, 2.5, 3.75};
  assert(t_doubles[0].as_double() == 1.25);
  assert(t_doubles[1].as_double() == 2.5);
  assert(t_doubles[2].as_double() == 3.75);

  SqliteValueTuple<3> t_strs = {"north", "south", "east"};
  assert(t_strs[0].as_text() == SqliteStringView("north"));
  assert(t_strs[1].as_text() == SqliteStringView("south"));
  assert(t_strs[2].as_text() == SqliteStringView("east"));

  SqliteValueTuple<1> t_prim_int(42);
  assert(t_prim_int[0].as_int() == 42);

  SqliteValueTuple<1> t_prim_double(3.14);
  assert(t_prim_double[0].as_double() > 3.13);

  SqliteValueTuple<1> t_prim_str("hello_world");
  assert(t_prim_str[0].as_text() == SqliteStringView("hello_world"));

  int native_ints[2] = {500, 600};
  SqliteValueTuple<4> t_from_native_ints(native_ints);
  assert(t_from_native_ints[0].as_int() == 500);
  assert(t_from_native_ints[1].as_int() == 600);
  assert(t_from_native_ints[2].is_null());
  assert(t_from_native_ints[3].is_null());

  double native_dbls[2] = {9.1, 9.2};
  SqliteValueVec<4> v_from_native_dbls(native_dbls);
  assert(v_from_native_dbls.size() == 2);
  assert(v_from_native_dbls[0].as_double() > 9.0);
  assert(v_from_native_dbls[1].as_double() > 9.1);

  SqliteValueVec<4> v_ints = {10, 20, 30};
  assert(v_ints.size() == 3);
  assert(v_ints[0].as_int() == 10);
  assert(v_ints[2].as_int() == 30);

  SqliteValueVec<4> v_strs = {"apple", "banana"};
  assert(v_strs.size() == 2);
  assert(v_strs[0].as_text() == SqliteStringView("apple"));
  assert(v_strs[1].as_text() == SqliteStringView("banana"));

  // 13. Test Variadic Heterogeneous Constructors (all different types)
  SqliteValueTuple<3> t_hetero_args(1001, "US-WEST-2", 99.75);
  assert(t_hetero_args[0].as_int() == 1001);
  assert(t_hetero_args[1].as_text() == SqliteStringView("US-WEST-2"));
  assert(t_hetero_args[2].as_double() > 99.7);

  SqliteValueVec<4> v_hetero_args(42, "vector_data", 3.14159);
  assert(v_hetero_args.size() == 3);
  assert(v_hetero_args[0].as_int() == 42);
  assert(v_hetero_args[1].as_text() == SqliteStringView("vector_data"));
  assert(v_hetero_args[2].as_double() > 3.14);

  SqliteValueTuple<> t9_hetero(1, "two", 3.0, 4, "five", 6.0, 7, "eight", 9.0);
  assert(t9_hetero.size() == 9);
  assert(t9_hetero[0].as_int() == 1);
  assert(t9_hetero[1].as_text() == SqliteStringView("two"));
  assert(t9_hetero[8].as_double() == 9.0);

  printf("   [PASS] Static Null Template & SIMD Initialization verified.\n");
}

static void test_generic_constructors_and_variadic_pack() {
  printf("9. Testing Generic Array, Initializer List & Variadic Heterogeneous "
         "Constructors...\n");

  // 1. Variadic Heterogeneous Constructors across arities 2, 4, 5, 8, 10
  SqliteValueTuple<2> t2(101, 202.5);
  assert(t2.size() == 2);
  assert(t2[0].as_int() == 101);
  assert(t2[1].as_double() == 202.5);

  SqliteValueTuple<4> t4(10, "second_col", 30.5, true);
  assert(t4.size() == 4);
  assert(t4[0].as_int() == 10);
  assert(t4[1].as_text() == SqliteStringView("second_col"));
  assert(t4[2].as_double() == 30.5);
  assert(t4[3].as_bool() == true);

  SqliteValueTuple<5> t5(int64_t(9999999999LL), 3.14f, "text_val",
                         SqliteStringView("view_val"), 123);
  assert(t5.size() == 5);
  assert(t5[0].as_int64() == 9999999999LL);
  assert(t5[1].as_double() > 3.13 && t5[1].as_double() < 3.15);
  assert(t5[2].as_text() == SqliteStringView("text_val"));
  assert(t5[3].as_text() == SqliteStringView("view_val"));
  assert(t5[4].as_int() == 123);

  SqliteValueTuple<8> t8(1, 2.0, "three", true, int64_t(55), "six", 7.7, 88);
  assert(t8.size() == 8);
  assert(t8[0].as_int() == 1);
  assert(t8[1].as_double() == 2.0);
  assert(t8[2].as_text() == SqliteStringView("three"));
  assert(t8[3].as_bool() == true);
  assert(t8[4].as_int64() == 55);
  assert(t8[5].as_text() == SqliteStringView("six"));
  assert(t8[6].as_double() == 7.7);
  assert(t8[7].as_int() == 88);

  // Heap-allocated tuple (N = 0) with 10 heterogeneous variadic arguments
  SqliteValueTuple<> t10(1, 2.0, "three", true, int64_t(5), "six", 7.0, 8,
                         "nine", 10.0);
  assert(t10.size() == 10);
  assert(t10[0].as_int() == 1);
  assert(t10[2].as_text() == SqliteStringView("three"));
  assert(t10[8].as_text() == SqliteStringView("nine"));
  assert(t10[9].as_double() == 10.0);

  // 2. Partial filling via variadic constructor: trailing slots must be
  // SQLITE_NULL
  SqliteValueTuple<6> t6_partial(100, "twenty");
  assert(t6_partial.size() == 6);
  assert(t6_partial[0].as_int() == 100);
  assert(t6_partial[1].as_text() == SqliteStringView("twenty"));
  assert(t6_partial[2].is_null());
  assert(t6_partial[3].is_null());
  assert(t6_partial[4].is_null());
  assert(t6_partial[5].is_null());

  // 3. Adaptive vector variadic constructors (in-situ SBO and heap spilling)
  SqliteValueVec<4> v4(10, "alpha", 20.5);
  assert(v4.size() == 3);
  assert(v4.is_inline());
  assert(v4[0].as_int() == 10);
  assert(v4[1].as_text() == SqliteStringView("alpha"));
  assert(v4[2].as_double() == 20.5);

  // Variadic call with 6 arguments spills SqliteValueVec<4> to heap
  SqliteValueVec<4> v4_spill(1, 2.0, "three", 4, "five", 6.0);
  assert(v4_spill.size() == 6);
  assert(!v4_spill.is_inline());
  assert(v4_spill[0].as_int() == 1);
  assert(v4_spill[2].as_text() == SqliteStringView("three"));
  assert(v4_spill[4].as_text() == SqliteStringView("five"));
  assert(v4_spill[5].as_double() == 6.0);

  // Direct heap vector (N = 0) variadic constructor
  SqliteValueVec<> v12(1, "two", 3.0, 4, "five", 6.0, 7, "eight", 9.0, 10,
                       "eleven", 12.0);
  assert(v12.size() == 12);
  assert(v12[0].as_int() == 1);
  assert(v12[11].as_double() == 12.0);

  // 4. Native C-Arrays of various fundamental types
  int64_t arr_i64[2] = {10000000001LL, 10000000002LL};
  SqliteValueTuple<3> t_i64(arr_i64);
  assert(t_i64[0].as_int64() == 10000000001LL);
  assert(t_i64[1].as_int64() == 10000000002LL);
  assert(t_i64[2].is_null());

  float arr_flt[3] = {1.5f, 2.5f, 3.5f};
  SqliteValueTuple<3> t_flt(arr_flt);
  assert(t_flt[0].as_double() == 1.5);
  assert(t_flt[1].as_double() == 2.5);
  assert(t_flt[2].as_double() == 3.5);

  const char *arr_cstr[3] = {"foo", "bar", "baz"};
  SqliteValueTuple<3> t_cstr(arr_cstr);
  assert(t_cstr[0].as_text() == SqliteStringView("foo"));
  assert(t_cstr[1].as_text() == SqliteStringView("bar"));
  assert(t_cstr[2].as_text() == SqliteStringView("baz"));

  SqliteStringView arr_sv[2] = {SqliteStringView("s1"), SqliteStringView("s2")};
  SqliteValueTuple<3> t_sv(arr_sv);
  assert(t_sv[0].as_text() == SqliteStringView("s1"));
  assert(t_sv[1].as_text() == SqliteStringView("s2"));
  assert(t_sv[2].is_null());

  bool arr_bool[3] = {true, false, true};
  SqliteValueTuple<4> t_bool(arr_bool);
  assert(t_bool[0].as_bool() == true);
  assert(t_bool[1].as_bool() == false);
  assert(t_bool[2].as_bool() == true);
  assert(t_bool[3].is_null());

  // 5. Contiguous Pointer + Count Dynamic Slicing
  int data_pool[5] = {10, 20, 30, 40, 50};
  SqliteValueTuple<3> t_slice(data_pool + 1, 3);
  assert(t_slice[0].as_int() == 20);
  assert(t_slice[1].as_int() == 30);
  assert(t_slice[2].as_int() == 40);

  SqliteValueVec<4> v_slice(data_pool + 2, 2);
  assert(v_slice.size() == 2);
  assert(v_slice[0].as_int() == 30);
  assert(v_slice[1].as_int() == 40);

  SqliteValueTuple<3> t_empty_slice(static_cast<const int *>(nullptr), 0);
  assert(t_empty_slice[0].is_null());
  assert(t_empty_slice[1].is_null());
  assert(t_empty_slice[2].is_null());

  // 6. Initializer List Syntax (`= { ... }`)
  SqliteValueTuple<3> t_il = {"red", "green", "blue"};
  assert(t_il[0].as_text() == SqliteStringView("red"));
  assert(t_il[1].as_text() == SqliteStringView("green"));
  assert(t_il[2].as_text() == SqliteStringView("blue"));

  SqliteValueVec<4> v_il = {1.1, 2.2, 3.3, 4.4};
  assert(v_il.size() == 4);
  assert(v_il[0].as_double() == 1.1);
  assert(v_il[3].as_double() == 4.4);

  // 7. Single Primitive Initializing Constructors
  SqliteValueTuple<1> t_s_int(12345);
  assert(t_s_int[0].as_int() == 12345);

  SqliteValueTuple<1> t_s_i64(int64_t(9876543210LL));
  assert(t_s_i64[0].as_int64() == 9876543210LL);

  SqliteValueTuple<1> t_s_dbl(2.718281828);
  assert(t_s_dbl[0].as_double() == 2.718281828);

  SqliteValueTuple<1> t_s_flt(3.14159f);
  assert(t_s_flt[0].as_double() > 3.14 && t_s_flt[0].as_double() < 3.15);

  SqliteValueTuple<1> t_s_str("embedded_sys");
  assert(t_s_str[0].as_text() == SqliteStringView("embedded_sys"));

  SqliteValueTuple<1> t_s_sv(SqliteStringView("custom_sv"));
  assert(t_s_sv[0].as_text() == SqliteStringView("custom_sv"));

  SqliteValueVec<2> v_s_dbl(99.9);
  assert(v_s_dbl.size() == 1);
  assert(v_s_dbl[0].as_double() == 99.9);

  SqliteValueVec<2> v_s_str("vector_str");
  assert(v_s_str.size() == 1);
  assert(v_s_str[0].as_text() == SqliteStringView("vector_str"));

  printf("   [PASS] Generic Array, Initializer List & Variadic Heterogeneous "
         "Constructors verified.\n");
}

// ============================================================================
// 10. Dynamic Vector Growth, Capacity Scaling & SBO Spilling Stress Tests
// ============================================================================
static void test_value_vec_dynamic_growth_and_capacity() {
  printf("10. Testing Dynamic Vector Growth, Capacity Scaling & SBO Spilling "
         "Stress Tests...\n");

  // 1. Incremental Growth on SqliteValueVec<1> (1-byte SBO to 128 elements)
  SqliteValueVec<1> v1;
  assert(v1.empty());
  assert(v1.size() == 0);
  assert(v1.capacity() == 1);
  assert(v1.is_inline());

  v1.push_back(1001);
  assert(v1.size() == 1);
  assert(v1.capacity() == 1);
  assert(v1.is_inline());
  assert(v1[0] == 1001);

  // Spills to heap on 2nd element
  v1.push_back(1002);
  assert(v1.size() == 2);
  assert(v1.capacity() >= 2);
  assert(!v1.is_inline());
  assert(v1[0] == 1001);
  assert(v1[1] == 1002);

  // Push up to 128 elements to test geometric capacity expansion
  for (int i = 2; i < 128; ++i) {
    v1.push_back(1000 + i + 1);
    assert(v1.size() == i + 1);
    assert(v1.capacity() >= v1.size());
  }
  assert(v1.size() == 128);
  for (int i = 0; i < 128; ++i) {
    assert(v1[i].as_int() == 1000 + i + 1);
  }

  // 2. Exact SBO Stack Spill Boundary Tests (N = 4 and N = 8)
  SqliteValueVec<4> v4;
  for (int i = 0; i < 4; ++i) {
    v4.push_back(i * 10);
    assert(v4.is_inline());
  }
  assert(v4.size() == 4);
  assert(v4.is_inline());
  v4.push_back(40); // 5th element triggers heap spill
  assert(v4.size() == 5);
  assert(!v4.is_inline());
  for (int i = 0; i <= 4; ++i)
    assert(v4[i].as_int() == i * 10);

  SqliteValueVec<8> v8;
  for (int i = 0; i < 8; ++i) {
    v8.push_back(i * 100);
    assert(v8.is_inline());
  }
  assert(v8.size() == 8);
  assert(v8.is_inline());
  v8.push_back(800); // 9th element triggers heap spill
  assert(v8.size() == 9);
  assert(!v8.is_inline());
  for (int i = 0; i <= 8; ++i)
    assert(v8[i].as_int() == i * 100);

  // 3. Heterogeneous Primitive push_back & emplace_back Types
  SqliteValueVec<2> v_mixed;
  v_mixed.push_back(42);                              // int
  v_mixed.push_back(int64_t(9000000000000LL));        // int64_t
  v_mixed.push_back(3.1415926535);                    // double
  v_mixed.push_back(2.718f);                          // float
  v_mixed.push_back(true);                            // bool
  v_mixed.push_back("dynamic_c_str");                 // const char*
  v_mixed.push_back(SqliteStringView("string_view")); // SqliteStringView
  const uint8_t blob_data[] = {0xDE, 0xAD, 0xBE, 0xEF};
  v_mixed.push_back(SqliteBlobView(blob_data, 4)); // SqliteBlobView
  v_mixed.emplace_back(7777);                      // emplace_back int
  v_mixed.emplace_back("emplaced_text");           // emplace_back text

  assert(v_mixed.size() == 10);
  assert(v_mixed[0].as_int() == 42);
  assert(v_mixed[1].as_int64() == 9000000000000LL);
  assert(v_mixed[2].as_double() == 3.1415926535);
  assert(v_mixed[3].as_double() > 2.71 && v_mixed[3].as_double() < 2.72);
  assert(v_mixed[4].as_bool() == true);
  assert(v_mixed[5].as_text() == SqliteStringView("dynamic_c_str"));
  assert(v_mixed[6].as_text() == SqliteStringView("string_view"));
  assert(v_mixed[7].as_blob() == SqliteBlobView(blob_data, 4));
  assert(v_mixed[8].as_int() == 7777);
  assert(v_mixed[9].as_text() == SqliteStringView("emplaced_text"));

  // 4. pop_back() down to empty
  while (!v_mixed.empty()) {
    int before_sz = v_mixed.size();
    v_mixed.pop_back();
    assert(v_mixed.size() == before_sz - 1);
  }
  assert(v_mixed.empty());
  assert(v_mixed.size() == 0);

  // 5. reserve() Stress & Pre-Allocation
  SqliteValueVec<1> v_res;
  v_res.reserve(200);
  assert(v_res.capacity() >= 200);
  assert(v_res.empty());
  assert(v_res.size() == 0);

  int cap_before = v_res.capacity();
  for (int i = 0; i < 200; ++i) {
    v_res.push_back(i * 3);
  }
  assert(v_res.size() == 200);
  assert(v_res.capacity() == cap_before); // No reallocation occurred!
  for (int i = 0; i < 200; ++i) {
    assert(v_res[i].as_int() == i * 3);
  }

  // 6. clear() and Re-Push Lifecycle
  v_res.clear();
  assert(v_res.empty());
  assert(v_res.size() == 0);
  for (int i = 0; i < 10; ++i) {
    v_res.push_back(i + 100);
  }
  assert(v_res.size() == 10);
  for (int i = 0; i < 10; ++i) {
    assert(v_res[i].as_int() == i + 100);
  }

  // 7. Copy & Move Semantics Under Dynamic Heavy Heap Load
  SqliteValueVec<1> v_copy(v_res);
  assert(v_copy == v_res);
  assert(v_copy.size() == 10);
  v_copy[0] = 99999;
  assert(v_copy != v_res);
  assert(v_res[0].as_int() == 100); // Deep copy independence verified

  SqliteValueVec<1> v_move(sqlite_move(v_copy));
  assert(v_move.size() == 10);
  assert(v_move[0].as_int() == 99999);
  assert(v_copy.empty());

  // 8. Zeroed Spare Capacity Guarantee on Reserve & Spill
  SqliteValueVec<1> v_zero;
  v_zero.reserve(32);
  assert(v_zero.capacity() >= 32);
  assert(v_zero.data() != nullptr);
  const unsigned char *raw_v_bytes =
      reinterpret_cast<const unsigned char *>(v_zero.data());
  for (size_t i = 0; i < 32 * sizeof(SqliteValueOwned); ++i) {
    assert(raw_v_bytes[i] == 0x00);
  }

  printf("   [PASS] Dynamic Vector Growth, Capacity Scaling & SBO Spilling "
         "Stress Tests verified.\n");
}

// ============================================================================
// 11. Exhaustive Branch Coverage & Extreme Edge Cases Tests
// ============================================================================
static void test_exhaustive_edge_cases_and_coverage() {
  printf(
      "11. Testing Exhaustive Branch Coverage & Extreme Edge Cases Tests...\n");

  // 1. pop_back() on empty containers (no-op safety)
  SqliteValueVec<2> v_empty_stack;
  assert(v_empty_stack.empty());
  v_empty_stack.pop_back();
  assert(v_empty_stack.empty());
  assert(v_empty_stack.size() == 0);

  SqliteValueVec<> v_empty_heap;
  assert(v_empty_heap.size() == 0);
  assert(v_empty_heap.empty());
  v_empty_heap.pop_back();
  assert(v_empty_heap.empty());

  // 2. Negative resize clamping to 0
  SqliteValueVec<4> v_neg;
  v_neg.push_back(10);
  v_neg.push_back(20);
  assert(v_neg.size() == 2);
  v_neg.resize(-5);
  assert(v_neg.empty());
  assert(v_neg.size() == 0);

  SqliteValueVec<> v_neg_heap;
  v_neg_heap.resize(-100);
  assert(v_neg_heap.empty());
  assert(v_neg_heap.size() == 0);

  // 3. Slicing Constructor Edge Cases (count < N, count > N, nullptr)
  int test_pool[5] = {111, 222, 333, 444, 555};

  // count < N (slots 2 and 3 must be null)
  SqliteValueTuple<4> t_under(test_pool, 2);
  assert(t_under[0].as_int() == 111);
  assert(t_under[1].as_int() == 222);
  assert(t_under[2].is_null());
  assert(t_under[3].is_null());

  // count > N (only first 4 taken)
  SqliteValueTuple<4> t_over(test_pool, 5);
  assert(t_over[0].as_int() == 111);
  assert(t_over[3].as_int() == 444);

  // nullptr with non-zero count (all null)
  SqliteValueTuple<4> t_null_src(static_cast<const int *>(nullptr), 4);
  assert(t_null_src[0].is_null());
  assert(t_null_src[3].is_null());

  // 4. Self-Assignment Safety (Stack & Heap)
  SqliteValueTuple<3> t_self = {10, 20, 30};
  SqliteValueTuple<3> &t_self_ref = t_self;
  t_self = t_self_ref; // Self copy
  assert(t_self[0].as_int() == 10);
  assert(t_self[2].as_int() == 30);

  SqliteValueTuple<> t_self_heap(10);
  t_self_heap[0] = 500;
  SqliteValueTuple<> &t_self_heap_ref = t_self_heap;
  t_self_heap = t_self_heap_ref; // Heap self copy
  assert(t_self_heap[0].as_int() == 500);

  SqliteValueVec<4> v_self;
  v_self.push_back(100);
  SqliteValueVec<4> &v_self_ref = v_self;
  v_self = v_self_ref; // Stack vec self copy
  assert(v_self.size() == 1);
  assert(v_self[0].as_int() == 100);

  v_self.resize(10); // Spill to heap
  v_self[0] = 999;
  SqliteValueVec<4> &v_self_hp_ref = v_self;
  v_self = v_self_hp_ref; // Heap vec self copy
  assert(v_self.size() == 10);
  assert(v_self[0].as_int() == 999);

  // 5. Cross-State Copy & Move Assignments
  SqliteValueVec<4> v_stk;
  v_stk.push_back(1);
  v_stk.push_back(2);

  SqliteValueVec<4> v_hp;
  v_hp.resize(6);
  for (int i = 0; i < 6; ++i)
    v_hp[i] = (i + 1) * 10;

  // Stack assigned from Heap
  v_stk = v_hp;
  assert(!v_stk.is_inline());
  assert(v_stk.size() == 6);
  assert(v_stk[5].as_int() == 60);

  // Heap assigned from Stack
  SqliteValueVec<4> v_stk2;
  v_stk2.push_back(777);
  v_hp = v_stk2;
  assert(v_hp.is_inline());
  assert(v_hp.size() == 1);
  assert(v_hp[0].as_int() == 777);

  // 6. Repeated Shrinking and Expanding across SBO boundary (4 -> 8 -> 2 -> 16
  // -> 0 -> 4)
  SqliteValueVec<4> v_cycle;
  assert(v_cycle.is_inline());

  v_cycle.resize(4);
  assert(v_cycle.is_inline());
  assert(v_cycle.size() == 4);

  v_cycle.resize(8);
  assert(!v_cycle.is_inline());
  assert(v_cycle.size() == 8);

  v_cycle.resize(2);
  assert(v_cycle.is_inline()); // Returned to stack!
  assert(v_cycle.size() == 2);

  v_cycle.resize(16);
  assert(!v_cycle.is_inline()); // Spilled to heap!
  assert(v_cycle.size() == 16);

  v_cycle.clear();
  assert(v_cycle.empty());
  assert(v_cycle.size() == 0);

  v_cycle.resize(4);
  assert(v_cycle.is_inline()); // Cleanly back on stack!
  assert(v_cycle.size() == 4);

  // 7. Iterators on Empty Containers
  SqliteValueVec<2> v_empty_iter;
  assert(v_empty_iter.begin() == v_empty_iter.end());

  // 8. Scope Dispatcher Negative Size Edge Case
  bool ran_cb = false;
  withSqliteRowOwned(-5, [&](SqliteRowOwnedWrapper span) {
    assert(span.size() == 0);
    ran_cb = true;
  });
  assert(ran_cb);

  printf("   [PASS] Exhaustive Branch Coverage & Extreme Edge Cases Tests "
         "verified.\n");
}

// ============================================================================
// 12. Complete std::array & std::vector Standard Library Alignment Tests
// ============================================================================
static void test_std_array_and_vector_alignment() {
  printf("12. Testing Complete std::array & std::vector Standard Library Alignment...\n");

  // ------------------------------------------------------------------------
  // A. SqliteValueTuple<N> (std::array Interface Alignment)
  // ------------------------------------------------------------------------
  {
    // 1. Static Tuple (N = 4)
    SqliteValueTuple<4> t_stk(10, 20, 30, 40);
    assert(t_stk.max_size() == 4);
    assert(t_stk.front().as_int() == 10);
    assert(t_stk.back().as_int() == 40);
    assert(t_stk.at(1).as_int() == 20);
    assert(t_stk.at(99).is_null()); // Safe out-of-bounds fallback

    // Reverse Iteration
    int expected_rev[4] = { 40, 30, 20, 10 };
    int rev_idx = 0;
    for (auto it = t_stk.rbegin(); it != t_stk.rend(); ++it) {
      assert(it->as_int() == expected_rev[rev_idx++]);
    }
    assert(rev_idx == 4);

    // Const Reverse Iteration
    const auto& c_t_stk = t_stk;
    rev_idx = 0;
    for (auto it = c_t_stk.crbegin(); it != c_t_stk.crend(); ++it) {
      assert(it->as_int() == expected_rev[rev_idx++]);
    }

    // fill() & swap()
    t_stk.fill(999);
    for (const auto& elem : t_stk) {
      assert(elem.as_int() == 999);
    }

    SqliteValueTuple<4> t_stk2(1, 2, 3, 4);
    t_stk.swap(t_stk2);
    assert(t_stk[0].as_int() == 1);
    assert(t_stk2[0].as_int() == 999);

    // 2. Heap Tuple (N = 0 / sized 10)
    SqliteValueTuple<> t_hp(10);
    assert(t_hp.max_size() == 10);
    t_hp.fill("tuple_fill");
    for (const auto& elem : t_hp) {
      assert(elem.as_text() == SqliteStringView("tuple_fill"));
    }

    SqliteValueTuple<> t_hp2(10);
    t_hp2.fill(12345);
    t_hp.swap(t_hp2);
    assert(t_hp[0].as_int() == 12345);
    assert(t_hp2[0].as_text() == SqliteStringView("tuple_fill"));
  }

  // ------------------------------------------------------------------------
  // B. SqliteRowOwnedWrapper (std::array Interface Alignment)
  // ------------------------------------------------------------------------
  {
    SqliteValueOwned vals[3] = { SqliteValueOwned(111), SqliteValueOwned(222), SqliteValueOwned(333) };
    SqliteRowOwnedWrapper row_wrap(vals, 3);
    assert(row_wrap.front().as_int() == 111);
    assert(row_wrap.back().as_int() == 333);
    assert(row_wrap.at(1).as_int() == 222);

    int wrap_exp[3] = { 333, 222, 111 };
    int wrap_i = 0;
    for (auto it = row_wrap.rbegin(); it != row_wrap.rend(); ++it) {
      assert(it->as_int() == wrap_exp[wrap_i++]);
    }
    assert(wrap_i == 3);

    row_wrap.fill(777);
    assert(vals[0].as_int() == 777 && vals[2].as_int() == 777);
  }

  // ------------------------------------------------------------------------
  // C. SqliteRowView (std::array Interface Alignment)
  // ------------------------------------------------------------------------
  {
    sqlite3* db = nullptr;
    sqlite3_open(":memory:", &db);
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT 444, 555, 666", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    SqliteRowView r_view(stmt);
    assert(r_view.max_size() > 0);
    assert(r_view.front().as_int() == 444);
    assert(r_view.back().as_int() == 666);
    assert(r_view.at(1).as_int() == 555);

    int rview_exp[3] = { 666, 555, 444 };
    int rview_i = 0;
    for (auto it = r_view.rbegin(); it != r_view.rend(); ++it) {
      assert(it->as_int() == rview_exp[rview_i++]);
    }
    assert(rview_i == 3);

    sqlite3_finalize(stmt);
    sqlite3_close(db);
  }

  // ------------------------------------------------------------------------
  // D. SqliteValueVec<N> (std::vector Interface Alignment)
  // ------------------------------------------------------------------------
  {
    // 1. SBO Vector (N = 4) - Element access & Reverse Iterators
    SqliteValueVec<4> v_stk;
    v_stk.push_back(100);
    v_stk.push_back(200);
    v_stk.push_back(300);
    assert(v_stk.front().as_int() == 100);
    assert(v_stk.back().as_int() == 300);
    assert(v_stk.at(1).as_int() == 200);
    assert(v_stk.at(99).is_null()); // Safe out-of-bounds fallback

    int v_exp_rev[3] = { 300, 200, 100 };
    int v_rev_i = 0;
    for (auto it = v_stk.rbegin(); it != v_stk.rend(); ++it) {
      assert(it->as_int() == v_exp_rev[v_rev_i++]);
    }
    assert(v_rev_i == 3);

    // 2. insert(pos, val) - Copy & Move & Multi-count
    auto it_ins1 = v_stk.insert(v_stk.begin(), 50);
    assert(it_ins1 == v_stk.begin());
    assert(v_stk.size() == 4);
    assert(v_stk[0].as_int() == 50);

    // Insert with SBO spill: [50, 100, 150, 200, 300]
    SqliteValueOwned val_150(150);
    auto it_ins2 = v_stk.insert(v_stk.begin() + 2, sqlite_move(val_150));
    assert(it_ins2 == v_stk.begin() + 2);
    assert(v_stk.size() == 5);
    assert(!v_stk.is_inline()); // Spilled to heap!
    assert(v_stk[0].as_int() == 50);
    assert(v_stk[1].as_int() == 100);
    assert(v_stk[2].as_int() == 150);
    assert(v_stk[3].as_int() == 200);
    assert(v_stk[4].as_int() == 300);

    // Insert count: insert 2 copies of 777 at index 1
    v_stk.insert(v_stk.begin() + 1, 2, 777);
    assert(v_stk.size() == 7);
    assert(v_stk[0].as_int() == 50);
    assert(v_stk[1].as_int() == 777);
    assert(v_stk[2].as_int() == 777);
    assert(v_stk[3].as_int() == 100);

    // 3. erase(pos) & erase(first, last)
    v_stk.erase(v_stk.begin() + 1, v_stk.begin() + 3);
    assert(v_stk.size() == 5);
    assert(v_stk[0].as_int() == 50);
    assert(v_stk[1].as_int() == 100);

    auto it_era = v_stk.erase(v_stk.begin());
    assert(it_era == v_stk.begin());
    assert(v_stk.size() == 4);
    assert(v_stk[0].as_int() == 100);

    // Deep Erase Destruction Verification: Erase inline and heap-allocated string payloads
    {
      SqliteValueVec<4> v_strings;
      v_strings.push_back("str_1");
      v_strings.push_back("str_2");
      v_strings.push_back("str_3");
      v_strings.push_back("str_4");
      v_strings.push_back("str_5");
      assert(v_strings.size() == 5);
      assert(!v_strings.is_inline());

      // Erase element in the middle (index 1) - moves elements [2..4] left by 1 and frees old index 1
      auto it_h1 = v_strings.erase(v_strings.begin() + 1);
      assert(it_h1 == v_strings.begin() + 1);
      assert(v_strings.size() == 4);
      assert(v_strings[0].as_text() == SqliteStringView("str_1"));
      assert(v_strings[1].as_text() == SqliteStringView("str_3"));
      assert(v_strings[2].as_text() == SqliteStringView("str_4"));
      assert(v_strings[3].as_text() == SqliteStringView("str_5"));

      // Erase range [1, 3) - erases elements 1 and 2, moving element 3 into index 1
      auto it_h2 = v_strings.erase(v_strings.begin() + 1, v_strings.begin() + 3);
      assert(it_h2 == v_strings.begin() + 1);
      assert(v_strings.size() == 2);
      assert(v_strings[0].as_text() == SqliteStringView("str_1"));
      assert(v_strings[1].as_text() == SqliteStringView("str_5"));

      // Erase last remaining elements
      v_strings.erase(v_strings.begin(), v_strings.end());
      assert(v_strings.empty());
      assert(v_strings.size() == 0);
    }

    // 4. shrink_to_fit() - Contracts back to stack SBO when size <= N
    v_stk.shrink_to_fit();
    assert(v_stk.is_inline());
    assert(v_stk.size() == 4);

    // 5. assign(count, val) & assign(first, last)
    v_stk.assign(3, "assigned_text");
    assert(v_stk.size() == 3);
    for (const auto& elem : v_stk) {
      assert(elem.as_text() == SqliteStringView("assigned_text"));
    }

    const int test_src[4] = { 11, 22, 33, 44 };
    v_stk.assign(test_src, test_src + 4);
    assert(v_stk.size() == 4);
    assert(v_stk[0].as_int() == 11);
    assert(v_stk[3].as_int() == 44);

    // 6. swap(other) - Exhaustive Multi-State Matrix
    {
      // Stack <-> Heap Swap
      SqliteValueVec<4> v_stk_swap;
      v_stk_swap.push_back(10);
      v_stk_swap.push_back(20);
      assert(v_stk_swap.is_inline());

      SqliteValueVec<4> v_hp_swap;
      for (int i = 0; i < 6; ++i) v_hp_swap.push_back(i * 100);
      assert(!v_hp_swap.is_inline());

      v_stk_swap.swap(v_hp_swap);
      assert(!v_stk_swap.is_inline());
      assert(v_stk_swap.size() == 6 && v_stk_swap[0].as_int() == 0 && v_stk_swap[5].as_int() == 500);
      assert(v_hp_swap.is_inline());
      assert(v_hp_swap.size() == 2 && v_hp_swap[0].as_int() == 10 && v_hp_swap[1].as_int() == 20);

      // Heap <-> Heap Swap
      SqliteValueVec<4> v_hp2;
      for (int i = 0; i < 8; ++i) v_hp2.push_back(i + 1);
      assert(!v_hp2.is_inline());

      v_stk_swap.swap(v_hp2);
      assert(v_stk_swap.size() == 8 && v_stk_swap[0].as_int() == 1 && v_stk_swap[7].as_int() == 8);
      assert(v_hp2.size() == 6 && v_hp2[0].as_int() == 0 && v_hp2[5].as_int() == 500);

      // Empty <-> Populated Swap
      SqliteValueVec<4> v_empty;
      v_empty.swap(v_stk_swap);
      assert(v_stk_swap.empty() && v_stk_swap.size() == 0);
      assert(v_empty.size() == 8 && v_empty[0].as_int() == 1);
    }

    // 7. resize(count, val) with values and primitives
    {
      SqliteValueVec<4> v_resize_val;
      v_resize_val.resize(3, 42);
      assert(v_resize_val.size() == 3);
      assert(v_resize_val.is_inline());
      for (const auto& elem : v_resize_val) {
        assert(elem.as_int() == 42);
      }

      // Grow with SBO spill and value padding
      v_resize_val.resize(6, "padding");
      assert(v_resize_val.size() == 6);
      assert(!v_resize_val.is_inline());
      assert(v_resize_val[0].as_int() == 42);
      assert(v_resize_val[2].as_int() == 42);
      assert(v_resize_val[3].as_text() == SqliteStringView("padding"));
      assert(v_resize_val[5].as_text() == SqliteStringView("padding"));

      // Contract back to stack
      v_resize_val.resize(2);
      assert(v_resize_val.size() == 2);
      assert(v_resize_val.is_inline());
      assert(v_resize_val[0].as_int() == 42);
      assert(v_resize_val[1].as_int() == 42);
    }

    // 8. Insertion at Extreme Boundaries (Begin, End, Middle)
    {
      SqliteValueVec<4> v_bound;
      v_bound.push_back(100);
      v_bound.push_back(200);

      // Prefix bulk insert: [1, 1, 1, 100, 200] -> spills to heap
      v_bound.insert(v_bound.begin(), 3, 1);
      assert(v_bound.size() == 5);
      assert(!v_bound.is_inline());
      assert(v_bound[0].as_int() == 1);
      assert(v_bound[2].as_int() == 1);
      assert(v_bound[3].as_int() == 100);
      assert(v_bound[4].as_int() == 200);

      // Suffix bulk insert (at end): [1, 1, 1, 100, 200, 999, 999]
      v_bound.insert(v_bound.end(), 2, 999);
      assert(v_bound.size() == 7);
      assert(v_bound[5].as_int() == 999);
      assert(v_bound[6].as_int() == 999);
    }

    // 9. Empty Container Zero-Crash & Safe Fallback Guarantees
    {
      SqliteValueVec<4> v_zero;
      assert(v_zero.empty());
      assert(v_zero.size() == 0);
      assert(v_zero.front().is_null());
      assert(v_zero.back().is_null());
      assert(v_zero.at(0).is_null());
      assert(v_zero.at(100).is_null());
      assert(v_zero[0].is_null());
      assert(v_zero[-1].is_null());

      // Safe erase / insert on empty vector
      v_zero.erase(v_zero.begin(), v_zero.end());
      assert(v_zero.empty());

      auto it_ins0 = v_zero.insert(v_zero.begin(), 777);
      assert(it_ins0 == v_zero.begin());
      assert(v_zero.size() == 1 && v_zero[0].as_int() == 777);
      assert(v_zero.front().as_int() == 777);
      assert(v_zero.back().as_int() == 777);
    }

    // 10. Const Iterators & Subscript Bounds Robustness
    {
      SqliteValueVec<4> v_mut;
      v_mut.push_back(10);
      v_mut.push_back(20);
      v_mut.push_back(30);

      const SqliteValueVec<4>& v_const = v_mut;
      assert(v_const.front().as_int() == 10);
      assert(v_const.back().as_int() == 30);
      assert(v_const.at(1).as_int() == 20);
      assert(v_const.at(999).is_null());
      assert(v_const[static_cast<size_t>(1)].as_int() == 20);
      assert(v_const[static_cast<size_t>(999)].is_null());
      assert(v_const[-10].is_null());

      int const_exp[3] = { 10, 20, 30 };
      int c_i = 0;
      for (auto it = v_const.cbegin(); it != v_const.cend(); ++it) {
        assert(it->as_int() == const_exp[c_i++]);
      }
      assert(c_i == 3);

      int const_rev_exp[3] = { 30, 20, 10 };
      int cr_i = 0;
      for (auto it = v_const.crbegin(); it != v_const.crend(); ++it) {
        assert(it->as_int() == const_rev_exp[cr_i++]);
      }
      assert(cr_i == 3);
    }

    // 11. Direct Heap Vector (N = 0) Modifiers & Reverse Iterators
    {
      SqliteValueVec<> v_dh;
      assert(v_dh.empty());
      assert(v_dh.front().is_null());
      assert(v_dh.back().is_null());

      v_dh.assign(5, 777);
      assert(v_dh.size() == 5);
      v_dh.shrink_to_fit();
      assert(v_dh.capacity() == 5);

      v_dh.insert(v_dh.begin() + 2, 888);
      assert(v_dh.size() == 6);
      assert(v_dh[2].as_int() == 888);

      v_dh.erase(v_dh.begin() + 2);
      assert(v_dh.size() == 5);
      assert(v_dh[2].as_int() == 777);

      v_dh.resize(8, 999);
      assert(v_dh.size() == 8);
      assert(v_dh[4].as_int() == 777);
      assert(v_dh[7].as_int() == 999);

      int dh_rev_cnt = 0;
      for (auto it = v_dh.rbegin(); it != v_dh.rend(); ++it) {
        assert(it->as_int() == 999 || it->as_int() == 777);
        ++dh_rev_cnt;
      }
      assert(dh_rev_cnt == 8);

      // Direct Heap Tuple (N = 0 / sized 10) reverse iteration & out-of-bounds
      const SqliteValueTuple<> t_dh_const(10);
      assert(t_dh_const.at(100).is_null());
      assert(t_dh_const[-1].is_null());
      int t_dh_rev_cnt = 0;
      for (auto it = t_dh_const.crbegin(); it != t_dh_const.crend(); ++it) {
        assert(it->is_null());
        ++t_dh_rev_cnt;
      }
      assert(t_dh_rev_cnt == 10);
    }
  }

  printf("   [PASS] Complete std::array & std::vector Standard Library Alignment verified.\n");
}

int main() {
  printf("=================================================================\n");
  printf("Running Value Containers (sqlite3_value_containers.hpp) Core Tests\n");
  printf("=================================================================\n");

  test_value_tuple_static();
  test_value_tuple_heap();
  test_value_vec_adaptive_sbo();
  test_value_vec_heap();
  test_containers_dispatch();
  test_containers_sqlite_sql();
  test_dispatch_framework();
  test_null_mechanics_and_simd_init();
  test_generic_constructors_and_variadic_pack();
  test_value_vec_dynamic_growth_and_capacity();
  test_exhaustive_edge_cases_and_coverage();
  test_std_array_and_vector_alignment();

  printf("=================================================================\n");
  printf("All Value Container Core Tests Passed Successfully (100%%)!\n");
  printf("=================================================================\n");
  return 0;
}
