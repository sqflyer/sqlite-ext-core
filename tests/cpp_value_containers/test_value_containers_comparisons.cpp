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
    SqliteValueTuple<>  t9_large(10, "alpha", 3.14, 40, "fifty", 60.0, 70, "eighty", 90.0);
    SqliteValueVec<>    v10_large(10, "alpha", 3.14, 40, "fifty", 60.0, 70, "eighty", 90.0);

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

// ============================================================================
// 6. SqliteRowOwnedView Comprehensive Cross-Container Matrix
// ============================================================================
static void test_row_owned_view_cross_container_matrix() {
    printf("6. Testing SqliteRowOwnedView Comprehensive Cross-Container Matrix...\n");

    // Setup source row: (500, "sensor_a", 99.85, 1)
    SqliteValueTuple<4> t4(500, "sensor_a", 99.85, 1);
    SqliteValueVec<4>   v4(500, "sensor_a", 99.85, 1);
    SqliteRowOwnedWrapper wrap = t4.view();

    // A. Contiguous array SqliteRowOwnedView
    SqliteRowOwnedView row_owned_arr(t4.data(), 4);
    assert_relops_equal(row_owned_arr, t4);
    assert_relops_equal(row_owned_arr, v4);
    assert_relops_equal(row_owned_arr, wrap);
    assert(row_owned_arr.hash() == t4.hash());

    // B. Pointer array SqliteRowOwnedView (Projecting PK columns 0 and 1: 500, "sensor_a")
    const SqliteValueOwned* pk_ptrs[2] = { &t4[0], &t4[1] };
    SqliteRowOwnedView row_owned_pk(pk_ptrs, 2);
    SqliteValueTuple<2> expected_pk(500, "sensor_a");
    SqliteValueVec<2>   expected_pk_vec(500, "sensor_a");

    assert_relops_equal(row_owned_pk, expected_pk);
    assert_relops_equal(row_owned_pk, expected_pk_vec);
    assert(row_owned_pk.hash() == expected_pk.hash());

    // C. Non-contiguous projection PK columns 0 and 2: (500, 99.85)
    const SqliteValueOwned* non_contig_ptrs[2] = { &t4[0], &t4[2] };
    SqliteRowOwnedView row_owned_non_contig(non_contig_ptrs, 2);
    SqliteValueTuple<2> expected_non_contig(500, 99.85);

    assert_relops_equal(row_owned_non_contig, expected_non_contig);
    assert(row_owned_non_contig.hash() == expected_non_contig.hash());

    // D. SqliteRowOwnedView vs SqliteRowView from SQLite statement
    sqlite3* db = nullptr;
    assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT 500, 'sensor_a', 99.85, 1;", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    SqliteRowView rview(stmt);

    assert_relops_equal(row_owned_arr, rview);
    assert(row_owned_arr.hash() == rview.hash());

    // E. 1-Column scalar comparisons with SqliteRowOwnedView
    SqliteValueOwned single_owned(777LL);
    SqliteRowOwnedView single_view(single_owned);
    assert_relops_equal(single_view, 777LL);
    assert_relops_equal(single_view, 777);
    assert_relops_equal(single_view, 777u);
    assert_relops_less(single_view, 800);
    assert_relops_less(single_view, 800.0);

    SqliteValueOwned single_txt_owned("epsilon");
    SqliteRowOwnedView single_txt_view(single_txt_owned);
    assert_relops_equal(single_txt_view, "epsilon");
    assert_relops_equal(single_txt_view, SqliteStringView("epsilon"));
    assert_relops_less(single_txt_view, "zeta");

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    printf("   [PASS] SqliteRowOwnedView Comprehensive Cross-Container Matrix verified.\n");
}

// ============================================================================
// 7. SBO String & Blob Capacity Transitions Matrix
// ============================================================================
static void test_sbo_and_blob_boundaries_comparison_matrix() {
    printf("7. Testing SBO String & Blob Capacity Transitions Matrix...\n");

    // SBO limits: String SBO is <= 13 chars, Blob SBO is <= 14 bytes
    const char* str_11 = "12345678901";       // 11 chars -> SBO inline
    const char* str_12 = "123456789012";      // 12 chars -> SBO inline
    const char* str_13 = "1234567890123";     // 13 chars -> SBO inline max

    SqliteValueTuple<3> t_sbo_strings(str_11, str_12, str_13);
    SqliteValueVec<3>   v_sbo_strings(str_11, str_12, str_13);
    SqliteRowOwnedView  view_sbo_strings(t_sbo_strings);

    assert_relops_equal(t_sbo_strings, v_sbo_strings);
    assert_relops_equal(t_sbo_strings, view_sbo_strings);
    assert_relops_equal(v_sbo_strings, view_sbo_strings);
    assert(t_sbo_strings.hash() == v_sbo_strings.hash());
    assert(t_sbo_strings.hash() == view_sbo_strings.hash());

    // Lexicographical inline string comparisons: "12345678901" < "123456789012" < "1234567890123"
    SqliteValueTuple<1> t_sbo11(str_11);
    SqliteValueTuple<1> t_sbo12(str_12);
    SqliteValueTuple<1> t_sbo13(str_13);

    assert_relops_less(t_sbo11, t_sbo12);
    assert_relops_less(t_sbo12, t_sbo13);
    assert_relops_less(t_sbo11, t_sbo13);

    // Heap-backed strings exceeding 13 chars created via SQLite statement
    sqlite3* db = nullptr;
    assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
    sqlite3_stmt* stmt_str = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT '12345678901234', '123456789012345';", -1, &stmt_str, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt_str) == SQLITE_ROW);

    SqliteValueOwned heap_s14(sqlite3_column_value(stmt_str, 0));
    SqliteValueOwned heap_s15(sqlite3_column_value(stmt_str, 1));
    assert(heap_s14.is_heap_allocated());
    assert(heap_s15.is_heap_allocated());

    SqliteValueTuple<1> t_heap14(heap_s14);
    SqliteValueTuple<1> t_heap15(heap_s15);

    assert_relops_less(t_sbo13, t_heap14);
    assert_relops_less(t_heap14, t_heap15);
    sqlite3_finalize(stmt_str);

    // Blobs: 10 bytes (inline SBO) vs 14 bytes (inline max SBO)
    uint8_t raw_b10[10];
    uint8_t raw_b14[14];
    memset(raw_b10, 0xAA, 10);
    memset(raw_b14, 0xAA, 14);

    SqliteBlobView bv10(raw_b10, 10);
    SqliteBlobView bv14(raw_b14, 14);

    SqliteValueTuple<2> t_blobs(bv10, bv14);
    SqliteValueVec<2>   v_blobs(bv10, bv14);
    SqliteRowOwnedView  view_blobs(t_blobs);

    assert_relops_equal(t_blobs, v_blobs);
    assert_relops_equal(t_blobs, view_blobs);
    assert(t_blobs.hash() == v_blobs.hash());
    assert(t_blobs.hash() == view_blobs.hash());

    // Length prefix comparison on identical byte prefixes
    SqliteValueTuple<1> t_b10(bv10);
    SqliteValueTuple<1> t_b14_single(bv14);
    assert_relops_less(t_b10, t_b14_single);

    // Heap-backed blobs exceeding 14 bytes created via SQLite statement
    sqlite3_stmt* stmt_blob = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT x'AAAAAAAAAAAAAAAAAAAAAAAAAA', x'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA';", -1, &stmt_blob, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt_blob) == SQLITE_ROW);

    SqliteValueOwned heap_b13(sqlite3_column_value(stmt_blob, 0));
    SqliteValueOwned heap_b16(sqlite3_column_value(stmt_blob, 1));
    assert(heap_b16.is_heap_allocated());

    SqliteValueTuple<1> t_heap_b16(heap_b16);
    assert_relops_less(t_b14_single, t_heap_b16);

    sqlite3_finalize(stmt_blob);
    sqlite3_close(db);

    printf("   [PASS] SBO String & Blob Capacity Transitions Matrix verified.\n");
}

// ============================================================================
// 8. Heterogeneous NULL Ordering & Mixed Type Permutations Matrix
// ============================================================================
static void test_heterogeneous_null_ordering_matrix() {
    printf("8. Testing Heterogeneous NULL Ordering & Mixed Type Permutations Matrix...\n");

    // NULL in column 0 vs Integer in column 0
    SqliteValueTuple<3> row_null_c0(SqliteValueOwned(), 10, "fixed");
    SqliteValueTuple<3> row_int_c0(0, 10, "fixed");
    assert_relops_less(row_null_c0, row_int_c0);

    // NULL in column 1
    SqliteValueTuple<3> row_null_c1(10, SqliteValueOwned(), "fixed");
    SqliteValueTuple<3> row_int_c1(10, 0, "fixed");
    assert_relops_less(row_null_c1, row_int_c1);

    // NULL in column 2
    SqliteValueTuple<3> row_null_c2(10, "fixed", SqliteValueOwned());
    SqliteValueTuple<3> row_int_c2(10, "fixed", 0);
    assert_relops_less(row_null_c2, row_int_c2);

    // Full 5-type ordering chain: NULL < INT < FLOAT < TEXT < BLOB
    uint8_t dummy_blob[2] = {0x01, 0x02};
    SqliteBlobView blob_val(dummy_blob, 2);

    SqliteValueTuple<2> r_null(100, SqliteValueOwned());
    SqliteValueTuple<2> r_int(100, 10);
    SqliteValueTuple<2> r_float(100, 10.0);
    SqliteValueTuple<2> r_text(100, "10");
    SqliteValueTuple<2> r_blob(100, blob_val);

    assert_relops_less(r_null, r_int);
    assert_relops_less(r_int, r_float);
    assert_relops_less(r_float, r_text);
    assert_relops_less(r_text, r_blob);

    // Transitivity checks
    assert_relops_less(r_null, r_blob);
    assert_relops_less(r_int, r_blob);

    printf("   [PASS] Heterogeneous NULL Ordering & Mixed Type Permutations Matrix verified.\n");
}

int main() {
    setbuf(stdout, NULL);
    printf("=================================================================\n");
    printf("Running Value Containers Relational Comparisons Tests\n");
    printf("=================================================================\n");

    test_cross_container_equality_matrix();
    test_ordering_differences_matrix();
    test_prefix_and_length_semantics();
    test_sqlite_type_rank_collation();
    test_scalar_primitive_relational_ops();
    test_row_owned_view_cross_container_matrix();
    test_sbo_and_blob_boundaries_comparison_matrix();
    test_heterogeneous_null_ordering_matrix();

    printf("=================================================================\n");
    printf("All Value Container Relational Comparison Tests Passed Successfully (100%%)!\n");
    printf("=================================================================\n");
    return 0;
}
