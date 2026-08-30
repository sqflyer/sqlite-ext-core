#define SQLITE_CORE
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sqlite3.h>
#include "sqlite3_row.hpp"
#include "sqlite3_value_containers.hpp"

// ============================================================================
// RELATIONAL TESTING UTILITIES
// ============================================================================

template <typename L, typename R>
static void assert_relops_equal(const L& left, const R& right) {
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
static void assert_relops_less(const L& left, const R& right) {
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

// ============================================================================
// 1. Cross-Container Equality Matrix (All 4 Container Representations)
// ============================================================================
static void test_cross_container_equality_matrix() {
    printf("1. Testing Cross-Container Equality Matrix...\n");

    // Setup sample row representations with identical contents: (10, "alpha", 3.14)
    SqliteValueTuple<3> t3(10, "alpha", 3.14);
    SqliteValueVec<4>   v4(10, "alpha", 3.14);
    SqliteValueTuple<9> t9_large(10, "alpha", 3.14, 40, "fifty", 60.0, 70, "eighty", 90.0);
    SqliteValueVec<10>  v10_large(10, "alpha", 3.14, 40, "fifty", 60.0, 70, "eighty", 90.0);

    SqliteRowOwnedWrapper wrap_t3 = t3.view();
    SqliteRowOwnedWrapper wrap_v4 = v4.view();
    SqliteRowOwnedWrapper wrap_t9 = t9_large.view();
    SqliteRowOwnedWrapper wrap_v10 = v10_large.view();

    // Setup SQLite in-memory statement to produce SqliteRowView
    sqlite3* db = nullptr;
    assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT 10, 'alpha', 3.14;", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    SqliteRowView rview(stmt);

    // Equality matrix across all 4 representations
    assert_relops_equal(t3, t3);
    assert_relops_equal(t3, v4);
    assert_relops_equal(t3, wrap_t3);
    assert_relops_equal(t3, wrap_v4);
    assert_relops_equal(t3, rview);

    assert_relops_equal(v4, v4);
    assert_relops_equal(v4, wrap_t3);
    assert_relops_equal(v4, wrap_v4);
    assert_relops_equal(v4, rview);

    assert_relops_equal(wrap_t3, wrap_t3);
    assert_relops_equal(wrap_t3, wrap_v4);
    assert_relops_equal(wrap_t3, rview);

    assert_relops_equal(rview, rview);

    // Large heap tuples & vectors equality
    assert_relops_equal(t9_large, t9_large);
    assert_relops_equal(t9_large, v10_large);
    assert_relops_equal(t9_large, wrap_t9);
    assert_relops_equal(t9_large, wrap_v10);
    assert_relops_equal(v10_large, v10_large);

    // Hash code equivalences for identical contents
    assert(t3.hash() == v4.hash());
    assert(t3.hash() == wrap_t3.hash());
    assert(t3.hash() == rview.hash());
    assert(t9_large.hash() == v10_large.hash());
    assert(t9_large.hash() == wrap_t9.hash());

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    printf("   [PASS] Cross-Container Equality Matrix verified.\n");
}

// ============================================================================
// 2. Lexicographical Ordering Matrix across Columns
// ============================================================================
static void test_ordering_differences_matrix() {
    printf("2. Testing Lexicographical Ordering Differences Matrix...\n");

    SqliteValueTuple<3> t3(10, "alpha", 3.14);
    SqliteValueVec<4>   v4(10, "alpha", 3.14);
    SqliteRowOwnedWrapper wrap_t3 = t3.view();

    sqlite3* db = nullptr;
    assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT 10, 'alpha', 3.14;", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    SqliteRowView rview(stmt);

    // Difference in last column (3.14 vs 3.15)
    SqliteValueTuple<3> t3_greater(10, "alpha", 3.15);
    SqliteValueVec<4>   v4_greater(10, "alpha", 3.15);
    assert_relops_less(t3, t3_greater);
    assert_relops_less(t3, v4_greater);
    assert_relops_less(v4, t3_greater);
    assert_relops_less(v4, v4_greater);
    assert_relops_less(wrap_t3, t3_greater);
    assert_relops_less(rview, v4_greater);

    // Difference in middle text column ("alpha" vs "beta")
    SqliteValueTuple<3> t3_beta(10, "beta", 1.0);
    assert_relops_less(t3, t3_beta);
    assert_relops_less(v4, t3_beta);
    assert_relops_less(rview, t3_beta);

    // Difference in first integer column (10 vs 11)
    SqliteValueTuple<3> t3_first_greater(11, "aaa", 0.0);
    assert_relops_less(t3, t3_first_greater);
    assert_relops_less(v4, t3_first_greater);
    assert_relops_less(rview, t3_first_greater);

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    printf("   [PASS] Lexicographical Ordering Differences Matrix verified.\n");
}

// ============================================================================
// 3. Arity / Length Prefix Semantics
// ============================================================================
static void test_prefix_and_length_semantics() {
    printf("3. Testing Arity / Length Prefix Semantics...\n");

    SqliteValueTuple<3> t3(10, "alpha", 3.14);
    SqliteValueVec<4>   v4(10, "alpha", 3.14);

    SqliteValueTuple<2> t2_prefix(10, "alpha");
    SqliteValueVec<2>   v2_prefix(10, "alpha");

    // Shorter common prefix sorts before longer row
    assert_relops_less(t2_prefix, t3);
    assert_relops_less(t2_prefix, v4);
    assert_relops_less(v2_prefix, t3);
    assert_relops_less(v2_prefix, v4);

    // Empty containers
    SqliteValueVec<4> v_empty;
    assert_relops_less(v_empty, t2_prefix);
    assert_relops_less(v_empty, t3);
    assert_relops_equal(v_empty, v_empty);

    printf("   [PASS] Arity / Length Prefix Semantics verified.\n");
}

// ============================================================================
// 4. SQLite Type-Rank Collation in Multi-Column Comparisons
// ============================================================================
static void test_sqlite_type_rank_collation() {
    printf("4. Testing SQLite Type-Rank Collation...\n");

    // NULL (0) < INTEGER/REAL (1) < TEXT (2) < BLOB (3)

    // NULL column vs Integer column
    SqliteValueTuple<2> t_null_first; // (NULL, NULL)
    SqliteValueTuple<2> t_int_first(1, 100);
    assert_relops_less(t_null_first, t_int_first);

    // Numeric tie-breaker: INTEGER (10) < FLOAT (10.0)
    SqliteValueTuple<2> t_int_pair(10, 20);
    SqliteValueTuple<2> t_dbl_pair(10.0, 20.0);
    assert_relops_less(t_int_pair, t_dbl_pair);
    assert(t_int_pair != t_dbl_pair);

    // Numeric vs Text: 10 < '10'
    SqliteValueTuple<1> t_num(10);
    SqliteValueTuple<1> t_txt("10");
    assert_relops_less(t_num, t_txt);

    // Text vs Blob: '10' < blob'10'
    uint8_t raw_blob_bytes[2] = { '1', '0' };
    SqliteBlobView bv(raw_blob_bytes, 2);
    SqliteValueTuple<1> t_blob(bv);
    assert_relops_less(t_txt, t_blob);

    printf("   [PASS] SQLite Type-Rank Collation verified.\n");
}

// ============================================================================
// 5. 1-Column Scalar / Primitive Relational Operators (All Fundamental Types)
// ============================================================================
static void test_scalar_primitive_relational_ops() {
    printf("5. Testing 1-Column Scalar / Primitive Relational Operators...\n");

    // Signed & unsigned integer overloads
    SqliteValueTuple<1> t1_int(42);
    assert_relops_equal(t1_int, 42);
    assert_relops_equal(t1_int, int64_t(42));
    assert_relops_equal(t1_int, 42u);
    assert_relops_equal(t1_int, 42UL);
    assert_relops_equal(t1_int, 42ULL);
    assert(t1_int != 42.0);
    assert_relops_less(t1_int, 50);
    assert_relops_less(t1_int, 50u);

    // Floating-point overloads
    SqliteValueTuple<1> t1_dbl(42.0);
    assert_relops_equal(t1_dbl, 42.0);
    assert_relops_equal(t1_dbl, 42.0f);
    assert(t1_dbl != 42);
    assert(t1_dbl != 42u);
    assert_relops_less(t1_dbl, 50.0);
    assert_relops_less(t1_dbl, 50.0f);

    // Vector scalar comparisons
    SqliteValueVec<1> v1_int = { 42 };
    assert_relops_equal(v1_int, 42);
    assert_relops_equal(v1_int, int64_t(42));
    assert_relops_equal(v1_int, 42u);
    assert_relops_equal(v1_int, 42UL);
    assert_relops_equal(v1_int, 42ULL);
    assert(v1_int != 42.0);
    assert_relops_less(v1_int, 50);

    SqliteValueVec<1> v1_dbl(42.0);
    assert_relops_equal(v1_dbl, 42.0);
    assert_relops_equal(v1_dbl, 42.0f);
    assert(v1_dbl != 42);
    assert_relops_less(v1_dbl, 50.0);

    // String views and owned strings
    SqliteValueTuple<1> t1_str("delta");
    assert_relops_equal(t1_str, "delta");
    assert_relops_equal(t1_str, SqliteStringView("delta"));
    SqliteStringOwned str_owned("delta");
    assert_relops_equal(t1_str, str_owned);
    assert_relops_less(t1_str, "echo");
    assert_relops_less(t1_str, SqliteStringView("echo"));

    // Booleans
    SqliteValueTuple<1> t1_bool(true);
    assert_relops_equal(t1_bool, true);
    assert_relops_equal(t1_bool, 1);

    // Direct comparison against SqliteValueOwned and SqliteValueView
    assert_relops_equal(t1_int, SqliteValueOwned(42));

    printf("   [PASS] 1-Column Scalar / Primitive Relational Operators verified.\n");
}

int main() {
    printf("=================================================================\n");
    printf("Running Value Containers Relational Comparisons Tests\n");
    printf("=================================================================\n");

    test_cross_container_equality_matrix();
    test_ordering_differences_matrix();
    test_prefix_and_length_semantics();
    test_sqlite_type_rank_collation();
    test_scalar_primitive_relational_ops();

    printf("=================================================================\n");
    printf("All Value Container Relational Comparison Tests Passed Successfully (100%%)!\n");
    printf("=================================================================\n");
    return 0;
}
