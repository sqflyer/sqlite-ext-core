#include <sqlite3.h>
#define SQLITE_CORE
#include "../../include/sqlite3_value.hpp"
#include "../../include/sqlite3_value_containers.hpp"
#include <stdio.h>
#include <assert.h>
#include <string.h>

void test_string_types(sqlite3* db) {
    SqliteStringOwned owned1(db);
    owned1.appendall("hello");
    
    SqliteStringOwned owned2(db);
    owned2.appendall("world");
    
    SqliteStringView view1("hello", 5);
    SqliteStringView view2("world", 5);

    assert(owned1.length() == 5);
    assert(owned1 == owned1);
    assert(owned1 != owned2);
    
    // Heterogeneous equality
    assert(view1 == owned1);
    assert(owned1 == view1);
    
    assert(view2 != owned1);
    assert(owned1 != view2);
    
    // Hash
    assert(owned1.hash() == view1.hash());
}

void test_blob_types() {
    char data[] = {0x01, 0x02, 0x03};
    SqliteBlobOwned owned(data, 3);
    SqliteBlobView view(data, 3);
    
    assert(owned.size() == 3);
    assert(owned == view);
    assert(view == owned);
    assert(owned.hash() == view.hash());
}

void test_value_types(sqlite3* db) {
    sqlite3_stmt* stmt_int = nullptr;
    sqlite3_stmt* stmt_text = nullptr;
    
    // Test Integer Value
    sqlite3_prepare_v2(db, "SELECT 42;", -1, &stmt_int, nullptr);
    sqlite3_step(stmt_int);
    sqlite3_value* int_val = sqlite3_column_value(stmt_int, 0);
    
    SqliteValueView view_int(int_val);
    SqliteValueOwned owned_int(int_val);
    
    assert(view_int.type() == SQLITE_INTEGER);
    assert(owned_int.type() == SQLITE_INTEGER);
    assert(view_int == owned_int);
    assert(owned_int == view_int);
    assert(!(view_int != owned_int));
    assert(view_int.hash() == owned_int.hash());
    
    // Test Text Value
    sqlite3_prepare_v2(db, "SELECT 'hello';", -1, &stmt_text, nullptr);
    sqlite3_step(stmt_text);
    sqlite3_value* text_val = sqlite3_column_value(stmt_text, 0);
    
    SqliteValueView view_text(text_val);
    SqliteValueOwned owned_text(text_val);
    
    assert(view_text.type() == SQLITE_TEXT);
    assert(owned_text.type() == SQLITE_TEXT);
    assert(view_text == owned_text);
    assert(view_text.hash() == owned_text.hash());
    
    // Heterogeneous polymorphic tests (Int vs Text)
    assert(view_int != view_text);
    assert(owned_int != owned_text);
    assert(view_int != owned_text);
    
    sqlite3_finalize(stmt_int);
    sqlite3_finalize(stmt_text);
}

void test_collation(sqlite3* db) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT NULL, 100, 0.5, 'hello', x'0102';", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    
    SqliteValueOwned val_null(sqlite3_column_value(stmt, 0));
    SqliteValueOwned val_int(sqlite3_column_value(stmt, 1));
    SqliteValueOwned val_float(sqlite3_column_value(stmt, 2));
    SqliteValueOwned val_text(sqlite3_column_value(stmt, 3));
    SqliteValueOwned val_blob(sqlite3_column_value(stmt, 4));
    
    // NULL is smallest
    assert(val_null < val_int);
    assert(val_null < val_float);
    assert(val_null < val_text);
    assert(val_null < val_blob);
    
    // Float(0.5) < Int(100)
    assert(val_float < val_int);
    
    // Numeric < Text
    assert(val_int < val_text);
    
    // Text < Blob
    assert(val_text < val_blob);
    
    sqlite3_finalize(stmt);
}

void test_bind(sqlite3* db) {
    SqliteStringOwned str(db);
    str.appendall("bind_test");
    
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT ?;", -1, &stmt, nullptr);
    
    // Test bind
    str.bind(stmt, 1);
    sqlite3_step(stmt);
    
    const unsigned char* res = sqlite3_column_text(stmt, 0);
    assert(strcmp((const char*)res, "bind_test") == 0);
    
    sqlite3_finalize(stmt);
}

static void dummy_udf(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    (void)argc;
    (void)argv;
    SqliteStringView str("result_test", 11);
    str.result(ctx);
}

void test_result(sqlite3* db) {
    sqlite3_create_function(db, "test_result_fn", 0, SQLITE_UTF8, nullptr, dummy_udf, nullptr, nullptr);
    
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT test_result_fn();", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    
    const unsigned char* res = sqlite3_column_text(stmt, 0);
    assert(strcmp((const char*)res, "result_test") == 0);
    
    sqlite3_finalize(stmt);
}

void test_string_builder(sqlite3* db) {
    SqliteStringOwned str(db);
    str.append("hello world", 5);
    str.appendchar(1, ' ');
    str.appendf("%d", 42);
    
    SqliteStringView view("hello 42", 8);
    assert(str == view);
    
    str.reset();
    assert(str.length() == 0);
}

void test_move_semantics() {
    SqliteStringOwned str1("move_test");
    SqliteStringOwned str2(static_cast<SqliteStringOwned&&>(str1));
    assert(str2.length() == 9);
    assert(str1.length() == 0 || str1.value() == nullptr);
    
    char blob_data[] = {0x01, 0x02};
    SqliteBlobOwned blob1(blob_data, 2);
    SqliteBlobOwned blob2(static_cast<SqliteBlobOwned&&>(blob1));
    assert(blob2.size() == 2);
    assert(blob1.size() == 0 || blob1.data() == nullptr);
}

void test_value_strict_equality(sqlite3* db) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT 5, 5.0, 5, '5';", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    
    SqliteValueOwned val_int1(sqlite3_column_value(stmt, 0));
    SqliteValueOwned val_float(sqlite3_column_value(stmt, 1));
    SqliteValueOwned val_int2(sqlite3_column_value(stmt, 2));
    SqliteValueOwned val_text(sqlite3_column_value(stmt, 3));
    
    // Int == Int
    assert(val_int1 == val_int2);
    // Strict typing: Int != Float
    assert(val_int1 != val_float);
    // Strict typing: Int != Text
    assert(val_int1 != val_text);
    
    sqlite3_finalize(stmt);
}

void test_heterogeneous_lookups(sqlite3* db) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT 42, 42.0, 'hello', x'0102';", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    
    SqliteValueOwned val_int(sqlite3_column_value(stmt, 0));
    SqliteValueOwned val_float(sqlite3_column_value(stmt, 1));
    SqliteValueOwned val_text(sqlite3_column_value(stmt, 2));
    SqliteValueOwned val_blob(sqlite3_column_value(stmt, 3));
    
    // 1. Value vs String Types
    SqliteStringView str_view("hello", 5);
    SqliteStringOwned str_owned("hello");
    assert(val_text == str_view);
    assert(str_view == val_text);
    assert(val_text == str_owned);
    assert(str_owned == val_text);
    assert(val_int != str_view); // Type mismatch
    
    // Implicit C-string lookups
    assert(val_text == "hello");
    assert("hello" == val_text);
    assert(val_text != "world");
    assert(val_int != "hello");
    
    // 2. Value vs Blob Types
    char blob_data[] = {0x01, 0x02};
    SqliteBlobView blob_view(blob_data, 2);
    assert(val_blob == blob_view);
    assert(blob_view == val_blob);
    assert(val_text != blob_view); // Type mismatch
    
    // 3. Value vs Primitives (Strict Typing)
    assert(val_int == 42);
    assert(42 == val_int);
    assert(val_int != 42.0); // Strict integer vs float
    assert(42.0 != val_int);
    
    assert(val_float == 42.0);
    assert(42.0 == val_float);
    assert(val_float != 42); // Strict float vs integer
    assert(42 != val_float);
    
    // Primitive inequalities
    assert(val_int < 50);
    assert(val_int > 10);
    assert(10 < val_int);
    
    assert(val_float < 50.0);
    assert(val_float > 10.0);

    // 4. Hash consistency (Important for unordered_map heterogeneous lookups)
    // - Value vs String hashes must match for identical strings
    assert(val_text.hash() == str_view.hash());
    assert(val_text.hash() == str_owned.hash());
    // - Value vs Blob hashes must match for identical binary payloads
    assert(val_blob.hash() == blob_view.hash());
    // - View and Owned versions of Values must produce identical hashes
    SqliteValueView val_text_view(sqlite3_column_value(stmt, 2));
    assert(val_text.hash() == val_text_view.hash());
    
    // - Primitive hash predictability (Allows map.find(42))
    sqlite3_int64 expected_i = 42;
    assert(val_int.hash() == SqliteHashUtil::hash(&expected_i, sizeof(expected_i)));
    
    double expected_d = 42.0;
    assert(val_float.hash() == SqliteHashUtil::hash(&expected_d, sizeof(expected_d)));
    
    // - Collision protections: ensure different types and different payloads differ
    assert(val_int.hash() != str_view.hash()); // int vs string
    assert(val_float.hash() != str_view.hash()); // float vs string
    assert(val_int.hash() != val_float.hash()); // 42 vs 42.0
    
    SqliteStringView diff_str("world", 5);
    assert(val_text.hash() != diff_str.hash()); // "hello" vs "world"
    
    sqlite3_finalize(stmt);
}

void test_coverage_edge_cases(sqlite3* db) {
    sqlite3_stmt* stmt;
    // SQLite evaluates 0.0/0.0 to NULL, so we create NaN directly in C++
    sqlite3_prepare_v2(db, "SELECT NULL, 5, 5.0, 'hello';", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    
    volatile double zero_val = 0.0;
    double nan_d = zero_val / zero_val;
    SqliteValueOwned val_nan(nan_d); // Uses our new zero-allocation double constructor!
    SqliteValueOwned val_null(sqlite3_column_value(stmt, 0));
    SqliteValueOwned val_int(sqlite3_column_value(stmt, 1));
    SqliteValueOwned val_float(sqlite3_column_value(stmt, 2));
    SqliteValueOwned val_text(sqlite3_column_value(stmt, 3));
    
    // 1. NaN checks
    assert(val_nan == nan_d);
    assert(nan_d == val_nan);
    assert(val_nan != 5.0);
    assert(!(val_nan < nan_d)); // NaN not less than NaN
    assert(val_nan < 5.0);      // NaN sorts before numbers
    assert(!(5.0 < val_nan));   // Numbers sort after NaN
    
    // 2. NULL vs Primitives
    assert(val_null != 5);
    assert(val_null < 5);      // NULL (0) < NUMERIC (1)
    assert(!(5 < val_null));
    assert(val_null < 5.0);
    assert(!(5.0 < val_null));
    
    // 3. String & Blob Inequalities
    SqliteStringView str_view("hello", 5);
    char blob_data[] = {0x01, 0x02};
    SqliteBlobView blob_view(blob_data, 2);
    
    assert(val_int < str_view);    // NUMERIC (1) < TEXT (2)
    assert(!(str_view < val_int)); 
    assert(val_int < blob_view);   // NUMERIC (1) < BLOB (3)
    assert(val_text < blob_view);  // TEXT (2) < BLOB (3)
    
    // 4. Tie-breakers between Int and Float
    assert(val_int < 6.0);         
    assert(val_float < 6);
    assert(val_int < 5.0);
    assert(!(val_float < 5));
    
    sqlite3_finalize(stmt);
}

void test_view_heterogeneous_lookups(sqlite3* db) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT 42, 42.0, 'hello', x'0102';", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    
    SqliteValueView view_int(sqlite3_column_value(stmt, 0));
    SqliteValueView view_float(sqlite3_column_value(stmt, 1));
    SqliteValueView view_text(sqlite3_column_value(stmt, 2));
    SqliteValueView view_blob(sqlite3_column_value(stmt, 3));
    
    // Primitives
    assert(view_int == 42);
    assert(42 == view_int);
    assert(view_int != 42.0);
    assert(view_float == 42.0);
    assert(view_float != 42);
    assert(view_int < 50);
    assert(view_float > 10.0);
    
    // Strings & Blobs
    SqliteStringView str_view("hello", 5);
    char blob_data[] = {0x01, 0x02};
    SqliteBlobView blob_view(blob_data, 2);
    
    assert(view_text == str_view);
    assert(str_view == view_text);
    assert(view_blob == blob_view);
    
    sqlite3_finalize(stmt);
}

void test_value_move_semantics() {
    SqliteValueOwned val1(42);
    SqliteValueOwned val2(static_cast<SqliteValueOwned&&>(val1));
    
    // val1 should be null, val2 should be 42
    assert(val1.type() == SQLITE_NULL);
    assert(val2.type() == SQLITE_INTEGER);
    assert(val2 == 42);
    
    SqliteValueOwned val3(5.0);
    val3 = static_cast<SqliteValueOwned&&>(val2);
    assert(val2.type() == SQLITE_NULL);
    assert(val3.type() == SQLITE_INTEGER);
    assert(val3 == 42);
}

void test_hashing() {
    SqliteValueOwned val_int(42);
    SqliteValueOwned val_float(42.0);
    volatile double zero_val = 0.0;
    SqliteValueOwned val_nan(zero_val / zero_val);
    
    // Hashes of identical numeric values must be completely different for map stability
    assert(val_int.hash() != val_float.hash());
    
    // NaN hash must be stable
    SqliteValueOwned val_nan2(zero_val / zero_val);
    assert(val_nan.hash() == val_nan2.hash());
}

void test_primitive_constructors() {
    SqliteValueOwned val_int(42);
    SqliteValueOwned val_int64(static_cast<sqlite3_int64>(9000000000LL));
    SqliteValueOwned val_float(3.14);
    
    assert(val_int.type() == SQLITE_INTEGER);
    assert(val_int64.type() == SQLITE_INTEGER);
    assert(val_float.type() == SQLITE_FLOAT);
    
    assert(val_int == 42);
    assert(val_int64 == 9000000000LL);
    assert(val_float == 3.14);
    
    // Ensure that primitive constructors trigger exact SBO storage
    assert(val_int.as_int64() == 42);
    assert(val_int64.as_int64() == 9000000000LL);
    assert(val_float.as_double() == 3.14);
}

void test_relational_operators(sqlite3* db) {
    SqliteValueOwned val_int(42);
    SqliteValueOwned val_float(3.14);
    
    // Primitives
    assert(val_int > 40);
    assert(40 < val_int);
    assert(val_int >= 42);
    assert(val_int <= 42);
    assert(val_float < 42.0);
    assert(42.0 > val_float);
    
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT 'hello', x'0102';", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    SqliteValueOwned val_str(sqlite3_column_value(stmt, 0));
    SqliteValueOwned val_blob(sqlite3_column_value(stmt, 1));
    
    // Strings
    SqliteStringOwned str_owned("hello");
    assert(val_str >= str_owned);
    assert(val_str <= str_owned);
    assert(val_str > SqliteStringView("hell"));
    
    // Blobs
    char blob1[] = {0x01, 0x02};
    char blob2[] = {0x01, 0x03};
    SqliteBlobView bv1(blob1, 2);
    SqliteBlobView bv2(blob2, 2);
    
    assert(val_blob >= bv1);
    assert(val_blob <= bv1);
    assert(bv2 > val_blob);
    assert(val_blob < bv2);
    
    sqlite3_finalize(stmt);
}

void test_cross_type_relational_operators(sqlite3* db) {
    SqliteValueOwned val_int(42);
    SqliteValueOwned val_float(3.14);
    
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT 'hello', x'0102';", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    SqliteValueOwned val_str(sqlite3_column_value(stmt, 0));
    SqliteValueOwned val_blob(sqlite3_column_value(stmt, 1));
    
    SqliteStringOwned str_owned("hello");
    char blob_data[] = {0x01, 0x02};
    SqliteBlobView bv(blob_data, 2);
    
    // NULL < NUMERIC < TEXT < BLOB
    
    // Numeric vs Text
    assert(val_int < str_owned);
    assert(str_owned > val_int);
    assert(val_float < str_owned);
    assert(val_str > 42);
    assert(val_str > 3.14);
    
    // Numeric vs Blob
    assert(val_int < bv);
    assert(bv > val_int);
    assert(val_float < bv);
    assert(val_blob > 42);
    
    // Text vs Blob
    assert(val_str < bv);
    assert(val_blob > str_owned);
    
    sqlite3_finalize(stmt);
}

void test_exhaustive_value_relational_operators(sqlite3* db) {
    // 1. Setup SQLite statement to produce SqliteValueView across all 5 datatypes
    sqlite3_stmt* stmt = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT NULL, 42, 3.14, 'alpha', X'01020304';", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);

    SqliteValueView v_null(sqlite3_column_value(stmt, 0));
    SqliteValueView v_int(sqlite3_column_value(stmt, 1));
    SqliteValueView v_float(sqlite3_column_value(stmt, 2));
    SqliteValueView v_text(sqlite3_column_value(stmt, 3));
    SqliteValueView v_blob(sqlite3_column_value(stmt, 4));

    // 2. Setup SqliteValueOwned counterparts
    SqliteValueOwned o_null;
    SqliteValueOwned o_int(42);
    SqliteValueOwned o_float(3.14);
    SqliteValueOwned o_text("alpha");
    const uint8_t blob_bytes[] = { 0x01, 0x02, 0x03, 0x04 };
    SqliteBlobView bv(blob_bytes, 4);
    SqliteValueOwned o_blob(bv);

    // Equality across identical representations
    assert(o_null == o_null); assert(v_null == v_null); assert(o_null == v_null); assert(v_null == o_null);
    assert(o_int == o_int);   assert(v_int == v_int);   assert(o_int == v_int);   assert(v_int == o_int);
    assert(o_float == o_float); assert(v_float == v_float); assert(o_float == v_float); assert(v_float == o_float);
    assert(o_text == o_text); assert(v_text == v_text); assert(o_text == v_text); assert(v_text == o_text);
    assert(o_blob == o_blob); assert(v_blob == v_blob); assert(o_blob == v_blob); assert(v_blob == o_blob);

    // Type Rank Collation Invariants: NULL (0) < NUMERIC (1) < TEXT (2) < BLOB (3)
    assert(o_null < o_int);   assert(v_null < v_int);   assert(o_null < v_int);   assert(v_null < o_int);
    assert(o_int < o_text);   assert(v_int < v_text);   assert(o_int < v_text);   assert(v_int < o_text);
    assert(o_text < o_blob);  assert(v_text < v_blob);  assert(o_text < v_blob);  assert(v_text < o_blob);

    // Scalar primitives comparison in both orientations
    assert(o_int == 42); assert(42 == o_int);
    assert(o_int == 42LL); assert(42LL == o_int);
    assert(o_int == 42u); assert(42u == o_int);
    assert(o_int == 42UL); assert(42UL == o_int);
    assert(o_int == 42ULL); assert(42ULL == o_int);
    assert(o_int < 50); assert(50 > o_int);

    assert(v_int == 42); assert(42 == v_int);
    assert(v_int == 42LL); assert(42LL == v_int);
    assert(v_int == 42u); assert(42u == v_int);
    assert(v_int < 50); assert(50 > v_int);

    assert(o_float == 3.14); assert(3.14 == o_float);
    assert(v_float == 3.14); assert(3.14 == v_float);

    assert(o_text == "alpha"); assert("alpha" == o_text);
    assert(o_text == SqliteStringView("alpha")); assert(SqliteStringView("alpha") == o_text);
    assert(v_text == "alpha"); assert("alpha" == v_text);
    assert(v_text == SqliteStringView("alpha")); assert(SqliteStringView("alpha") == v_text);

    assert(o_blob == bv); assert(bv == o_blob);
    assert(v_blob == bv); assert(bv == v_blob);

    sqlite3_finalize(stmt);
}

void test_as_text_and_as_blob(sqlite3* db) {
    sqlite3_stmt* stmt;
    assert(sqlite3_prepare_v2(db, "SELECT 'Hello, SQLite!', x'01020304FF', 12345, NULL;", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);

    sqlite3_value* v_text = sqlite3_column_value(stmt, 0);
    sqlite3_value* v_blob = sqlite3_column_value(stmt, 1);
    sqlite3_value* v_int  = sqlite3_column_value(stmt, 2);
    sqlite3_value* v_null = sqlite3_column_value(stmt, 3);

    // 1. Test SqliteValueView::as_text()
    SqliteValueView view_text(v_text);
    SqliteStringView sv1 = view_text.as_text();
    assert(sv1.length() == 14);
    assert(memcmp(sv1.data(), "Hello, SQLite!", 14) == 0);
    assert(sv1 == SqliteStringView("Hello, SQLite!", 14));

    // Null/empty view returns empty string view
    SqliteValueView view_null(v_null);
    SqliteStringView sv_null = view_null.as_text();
    assert(sv_null.length() == 0);

    SqliteValueView view_empty(nullptr);
    SqliteStringView sv_empty = view_empty.as_text();
    assert(sv_empty.length() == 0);
    assert(sv_empty.data() == nullptr);

    // 2. Test SqliteValueView::as_blob()
    SqliteValueView view_blob(v_blob);
    SqliteBlobView bv1 = view_blob.as_blob();
    assert(bv1.size() == 5);
    const unsigned char expected_bytes[] = {0x01, 0x02, 0x03, 0x04, 0xFF};
    assert(memcmp(bv1.data(), expected_bytes, 5) == 0);
    assert(bv1 == SqliteBlobView(expected_bytes, 5));

    SqliteBlobView bv_null = view_null.as_blob();
    assert(bv_null.size() == 0);

    SqliteBlobView bv_empty = view_empty.as_blob();
    assert(bv_empty.size() == 0);
    assert(bv_empty.data() == nullptr);

    // 3. Test SqliteValueOwned::as_text()
    SqliteValueOwned owned_text(v_text);
    SqliteStringView sv_owned = owned_text.as_text();
    assert(sv_owned.length() == 14);
    assert(memcmp(sv_owned.data(), "Hello, SQLite!", 14) == 0);
    assert(sv_owned == sv1);

    SqliteValueOwned owned_null(v_null);
    SqliteStringView sv_owned_null = owned_null.as_text();
    assert(sv_owned_null.length() == 0);
    assert(sv_owned_null.data() == nullptr);

    SqliteValueOwned owned_int(v_int);
    SqliteStringView sv_owned_int = owned_int.as_text();
    assert(sv_owned_int.length() == 0); // integer SBO has nullptr pValue

    // Move semantics on SqliteValueOwned invalidates pValue
    SqliteValueOwned moved_text = static_cast<SqliteValueOwned&&>(owned_text);
    assert(moved_text.as_text() == sv1);
    assert(owned_text.as_text().data() == nullptr);
    assert(owned_text.as_text().length() == 0);

    // 4. Test SqliteValueOwned::as_blob()
    SqliteValueOwned owned_blob(v_blob);
    SqliteBlobView bv_owned = owned_blob.as_blob();
    assert(bv_owned.size() == 5);
    assert(memcmp(bv_owned.data(), expected_bytes, 5) == 0);
    assert(bv_owned == bv1);

    SqliteBlobView bv_owned_null = owned_null.as_blob();
    assert(bv_owned_null.size() == 0);
    assert(bv_owned_null.data() == nullptr);

    SqliteValueOwned moved_blob = sqlite_move(owned_blob);
    assert(moved_blob.as_blob() == bv1);
    assert(owned_blob.as_blob().data() == nullptr);
    assert(owned_blob.as_blob().size() == 0);

    sqlite3_finalize(stmt);
}

void test_subtypes_and_affinities(sqlite3* db) {
    // 1. Exact 16-byte size guarantee
    static_assert(sizeof(SqliteValueOwned) == 16, "SqliteValueOwned must be exactly 16 bytes!");

    // 2. Default NULL constructor
    SqliteValueOwned val_null;
    assert(val_null.type() == SQLITE_NULL);
    assert(val_null.subtype() == SQLITE_SUBTYPE_NONE);
    assert(val_null.affinity() == SQLITE_AFF_NONE);
    assert(!val_null.is_json());
    assert(!val_null.is_bool());

    // 3. Integer with Subtype & Affinity
    SqliteValueOwned val_int(42LL, SQLITE_SUBTYPE_NONE, SQLITE_AFF_INTEGER);
    assert(val_int.type() == SQLITE_INTEGER);
    assert(val_int.as_int64() == 42);
    assert(val_int.affinity() == SQLITE_AFF_INTEGER);
    assert(sqlite3IsNumericAffinity(val_int.affinity()));

    // 4. Boolean factory constructor
    SqliteValueOwned val_bool = SqliteValueOwned::from_bool(true);
    assert(val_bool.type() == SQLITE_INTEGER);
    assert(val_bool.as_int64() == 1);
    assert(val_bool.subtype() == SQLITE_SUBTYPE_BOOL);
    assert(val_bool.is_bool());
    assert(!val_bool.is_json());

    // 5. Datetime timestamp factory constructor
    SqliteValueOwned val_time = SqliteValueOwned::from_datetime(1700000000000LL);
    assert(val_time.type() == SQLITE_INTEGER);
    assert(val_time.as_int64() == 1700000000000LL);
    assert(val_time.subtype() == SQLITE_SUBTYPE_DATETIME);
    assert(val_time.is_datetime());

    // 6. Subtype setters & predicates
    SqliteValueOwned val_sub(100LL);
    assert(val_sub.subtype() == SQLITE_SUBTYPE_NONE);
    val_sub.set_subtype(SQLITE_SUBTYPE_JSON);
    assert(val_sub.is_json());
    val_sub.set_subtype(SQLITE_SUBTYPE_DECIMAL);
    assert(val_sub.is_decimal());
    val_sub.set_subtype(SQLITE_SUBTYPE_UUID);
    assert(val_sub.is_uuid());
    val_sub.set_subtype(SQLITE_SUBTYPE_VECTOR);
    assert(val_sub.is_vector());
    val_sub.set_subtype(SQLITE_SUBTYPE_GEOMETRY);
    assert(val_sub.is_geometry());
    val_sub.set_subtype(SQLITE_SUBTYPE_COMPRESSED);
    assert(val_sub.is_compressed());

    // 7. Affinity setters
    val_sub.set_affinity(SQLITE_AFF_REAL);
    assert(val_sub.affinity() == SQLITE_AFF_REAL);
    assert(sqlite3IsNumericAffinity(val_sub.affinity()));

    // 8. Move semantics with subtype & affinity preservation
    SqliteValueOwned moved_sub = sqlite_move(val_sub);
    assert(moved_sub.is_compressed());
    assert(moved_sub.affinity() == SQLITE_AFF_REAL);
    assert(val_sub.subtype() == SQLITE_SUBTYPE_NONE);
    assert(val_sub.type() == SQLITE_NULL);

    // 9. SQL Subtype Execution & View reading
    sqlite3_stmt* stmt;
    assert(sqlite3_prepare_v2(db, "SELECT 999;", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_value* row_val = sqlite3_column_value(stmt, 0);
    SqliteValueView view_val(row_val);
    assert(view_val.type() == SQLITE_INTEGER);
    assert(view_val.affinity() == SQLITE_AFF_INTEGER);
    assert(sqlite3IsNumericAffinity(view_val.affinity()));
    assert(view_val.subtype() == SQLITE_SUBTYPE_NONE);
    assert(!view_val.is_json());
    assert(!view_val.is_bool());

    SqliteValueOwned copied_from_view(row_val);
    assert(copied_from_view.type() == SQLITE_INTEGER);
    assert(copied_from_view.as_int64() == 999);
    assert(copied_from_view.subtype() == SQLITE_SUBTYPE_NONE);
    assert(copied_from_view.affinity() == SQLITE_AFF_INTEGER);

    sqlite3_finalize(stmt);

    // 10. Test SqliteBlobOwned::release() zero-copy
    const char raw_bytes[] = {0x01, 0x02, 0x03, 0x04};
    SqliteBlobOwned rel_blob(raw_bytes, 4);
    assert(rel_blob.size() == 4);
    assert(rel_blob.is_valid());
    void* released_ptr = rel_blob.release();
    assert(released_ptr != nullptr);
    assert(rel_blob.data() == nullptr);
    assert(rel_blob.size() == 0);
    assert(rel_blob.is_valid());
    sqlite3_free(released_ptr);

    // 11. Dual Representation Struct assertions
    static_assert(sizeof(SqliteOwnedValueTag) == 1, "SqliteOwnedValueTag must be exactly 1 byte!");
    static_assert(sizeof(SqliteTypeRep) == 16, "SqliteTypeRep must be exactly 16 bytes!");
    static_assert(sizeof(InlineBufferRep) == 16, "InlineBufferRep must be exactly 16 bytes!");

    SqliteOwnedValueTag tag_test;
    tag_test.set(SQLITE_TEXT, false, 5);
    assert(tag_test.type() == SQLITE_TEXT);
    assert(!tag_test.is_heap());
    assert(tag_test.len() == 5);

    // 12. Inline SBO Strings (<= 13 chars) and Blobs (<= 14 bytes)
    SqliteValueOwned inline_str = SqliteValueOwned::from_text("hello world");
    assert(inline_str.type() == SQLITE_TEXT);
    assert(inline_str.tag().type() == SQLITE_TEXT);
    assert(!inline_str.is_heap_allocated());
    assert(!inline_str.tag().is_heap());
    assert(inline_str.inline_length() == 11);
    assert(inline_str.tag().len() == 11);
    assert(inline_str.as_text() == "hello world");

    SqliteValueOwned inline_blob = SqliteValueOwned::from_blob(raw_bytes, 4);
    assert(inline_blob.type() == SQLITE_BLOB);
    assert(!inline_blob.is_heap_allocated());
    assert(inline_blob.inline_length() == 4);
    assert(inline_blob.as_blob().size() == 4);

    // 13. Subtype Extraction & Query via SQLite Engine
    sqlite3_stmt* json_stmt;
    assert(sqlite3_prepare_v2(db, "SELECT json('{\"a\":1}');", -1, &json_stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(json_stmt) == SQLITE_ROW);
    SqliteValueView json_view(sqlite3_column_value(json_stmt, 0));
    assert(json_view.type() == SQLITE_TEXT);
    assert(json_view.is_json());
    assert(json_view.subtype() == SQLITE_SUBTYPE_JSON);

    SqliteValueOwned json_owned(sqlite3_column_value(json_stmt, 0));
    assert(json_owned.type() == SQLITE_TEXT);
    assert(json_owned.is_json());
    assert(json_owned.subtype() == SQLITE_SUBTYPE_JSON);
    assert(!json_owned.is_heap_allocated());
    assert(json_owned.as_text() == "{\"a\":1}");
    sqlite3_finalize(json_stmt);

    // 14. Inline Subtypes (<= 13 chars / 14 bytes)
    SqliteValueOwned inline_json = SqliteValueOwned::from_json("{\"a\":1}");
    assert(inline_json.type() == SQLITE_TEXT);
    assert(!inline_json.is_heap_allocated());
    assert(inline_json.subtype() == SQLITE_SUBTYPE_JSON);
    assert(inline_json.is_json());
    assert(inline_json.as_text() == "{\"a\":1}");

    SqliteValueOwned inline_dec = SqliteValueOwned::from_decimal("123.456");
    assert(inline_dec.type() == SQLITE_TEXT);
    assert(!inline_dec.is_heap_allocated());
    assert(inline_dec.subtype() == SQLITE_SUBTYPE_DECIMAL);
    assert(inline_dec.is_decimal());
    assert(inline_dec.as_text() == "123.456");

    SqliteValueOwned inline_comp = SqliteValueOwned::from_compressed(raw_bytes, 4);
    assert(inline_comp.type() == SQLITE_BLOB);
    assert(!inline_comp.is_heap_allocated());
    assert(inline_comp.subtype() == SQLITE_SUBTYPE_COMPRESSED);
    assert(inline_comp.is_compressed());
}

void test_value_view_ergonomics(sqlite3* db) {
    sqlite3_stmt* stmt = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT 42, 3.14, 'hello', X'DEADBEEF', NULL", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);

    // from_column
    SqliteValueView v_int = SqliteValueView::from_column(stmt, 0);
    SqliteValueView v_float = SqliteValueView::from_column(stmt, 1);
    SqliteValueView v_text = SqliteValueView::from_column(stmt, 2);
    SqliteValueView v_blob = SqliteValueView::from_column(stmt, 3);
    SqliteValueView v_null = SqliteValueView::from_column(stmt, 4);

    // Type predicates
    assert(v_int.is_integer());
    assert(v_int.is_numeric());
    assert(!v_int.is_null());
    assert(v_int.as_bool());
    assert(static_cast<bool>(v_int));

    assert(v_float.is_float());
    assert(v_float.is_numeric());
    assert(!v_float.is_null());

    assert(v_text.is_text());
    assert(!v_text.is_numeric());
    assert(!v_text.is_null());

    assert(v_blob.is_blob());
    assert(!v_blob.is_null());

    assert(v_null.is_null());
    assert(!v_null.is_integer());
    assert(!static_cast<bool>(v_null));

    // to_owned()
    SqliteValueOwned owned_int = v_int.to_owned();
    assert(owned_int.is_integer());
    assert(owned_int.as_int64() == 42);

    SqliteValueOwned owned_text = v_text.to_owned();
    assert(owned_text.is_text());
    assert(owned_text.as_text() == "hello");
    assert(!owned_text.is_heap_allocated()); // SBO!

    SqliteValueOwned owned_null = v_null.to_owned();
    assert(owned_null.is_null());
    assert(!static_cast<bool>(owned_null));

    sqlite3_finalize(stmt);
}

void test_all_subtype_factories(sqlite3* db) {
    // 1. from_bool
    SqliteValueOwned b_true = SqliteValueOwned::from_bool(true);
    assert(b_true.is_bool());
    assert(b_true.as_bool());
    assert(b_true.as_int64() == 1);
    assert(b_true.subtype() == SQLITE_SUBTYPE_BOOL);

    SqliteValueOwned b_false = SqliteValueOwned::from_bool(false);
    assert(b_false.is_bool());
    assert(!b_false.as_bool());
    assert(b_false.as_int64() == 0);

    // 2. from_datetime
    sqlite3_int64 epoch = 1724700000000LL;
    SqliteValueOwned dt = SqliteValueOwned::from_datetime(epoch);
    assert(dt.is_datetime());
    assert(dt.as_int64() == epoch);
    assert(dt.subtype() == SQLITE_SUBTYPE_DATETIME);

    // 3. from_json & from_jsonb
    SqliteValueOwned j_txt = SqliteValueOwned::from_json("{\"status\":\"ok\"}");
    assert(j_txt.is_json());
    assert(j_txt.is_text());
    assert(j_txt.subtype() == SQLITE_SUBTYPE_JSON);

    const uint8_t jsonb_data[] = {0x01, 0x02, 0x03, 0x04};
    SqliteValueOwned j_blob = SqliteValueOwned::from_jsonb(jsonb_data, 4);
    assert(j_blob.is_json());
    assert(j_blob.is_blob());
    assert(j_blob.subtype() == SQLITE_SUBTYPE_JSON);

    // 4. from_uuid
    const uint8_t uuid_bytes[16] = {0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    SqliteValueOwned uuid_val = SqliteValueOwned::from_uuid(uuid_bytes);
    assert(uuid_val.is_uuid());
    assert(uuid_val.is_blob());
    assert(uuid_val.subtype() == SQLITE_SUBTYPE_UUID);

    // 5. from_vector
    const float vec_data[3] = {1.0f, 2.0f, 3.0f};
    SqliteValueOwned vec_val = SqliteValueOwned::from_vector(vec_data, sizeof(vec_data));
    assert(vec_val.is_vector());
    assert(vec_val.is_blob());
    assert(vec_val.subtype() == SQLITE_SUBTYPE_VECTOR);

    // 6. from_geometry
    const uint8_t geo_data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    SqliteValueOwned geo_val = SqliteValueOwned::from_geometry(geo_data, 8);
    assert(geo_val.is_geometry());
    assert(geo_val.subtype() == SQLITE_SUBTYPE_GEOMETRY);

    // 7. from_decimal
    SqliteValueOwned dec_val = SqliteValueOwned::from_decimal("999999999999999.99");
    assert(dec_val.is_decimal());
    assert(dec_val.is_text());
    assert(dec_val.subtype() == SQLITE_SUBTYPE_DECIMAL);

    // 8. Bind & Result roundtrip in SQLite
    sqlite3_stmt* stmt = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT ?1", -1, &stmt, nullptr) == SQLITE_OK);
    b_true.bind(stmt, 1);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    SqliteValueView view(sqlite3_column_value(stmt, 0));
    assert(view.as_int64() == 1);
    assert(view.is_integer());
    sqlite3_finalize(stmt);
}

void test_from_literal() {
    // 1. NULL literals
    SqliteValueOwned n1 = SqliteValueOwned::from_literal("");
    assert(n1.is_null());
    assert(n1.type() == SQLITE_NULL);

    SqliteValueOwned n2 = SqliteValueOwned::from_literal("null");
    assert(n2.is_null());

    SqliteValueOwned n3 = SqliteValueOwned::from_literal("NULL");
    assert(n3.is_null());

    SqliteValueOwned n4 = SqliteValueOwned::from_literal((const char*)nullptr);
    assert(n4.is_null());

    // 2. Boolean literals
    SqliteValueOwned b1 = SqliteValueOwned::from_literal("true");
    assert(b1.is_bool() && b1.as_bool() == true && b1.type() == SQLITE_INTEGER);

    SqliteValueOwned b2 = SqliteValueOwned::from_literal("TRUE");
    assert(b2.is_bool() && b2.as_bool() == true);

    SqliteValueOwned b3 = SqliteValueOwned::from_literal("yes");
    assert(b3.is_bool() && b3.as_bool() == true);

    SqliteValueOwned b4 = SqliteValueOwned::from_literal("on");
    assert(b4.is_bool() && b4.as_bool() == true);

    SqliteValueOwned b5 = SqliteValueOwned::from_literal("false");
    assert(b5.is_bool() && b5.as_bool() == false);

    SqliteValueOwned b6 = SqliteValueOwned::from_literal("no");
    assert(b6.is_bool() && b6.as_bool() == false);

    SqliteValueOwned b7 = SqliteValueOwned::from_literal("off");
    assert(b7.is_bool() && b7.as_bool() == false);

    // 3. Integer literals
    SqliteValueOwned i1 = SqliteValueOwned::from_literal("0");
    assert(i1.is_integer() && i1.as_int64() == 0);

    SqliteValueOwned i2 = SqliteValueOwned::from_literal("1048576");
    assert(i2.is_integer() && i2.as_int64() == 1048576);

    SqliteValueOwned i3 = SqliteValueOwned::from_literal("-9876543210");
    assert(i3.is_integer() && i3.as_int64() == -9876543210LL);

    // 4. Floating-point literals
    SqliteValueOwned f1 = SqliteValueOwned::from_literal("3.14159265");
    assert(f1.is_float() && f1.as_double() > 3.14 && f1.as_double() < 3.15);

    SqliteValueOwned f2 = SqliteValueOwned::from_literal("-0.005");
    assert(f2.is_float() && f2.as_double() == -0.005);

    SqliteValueOwned f3 = SqliteValueOwned::from_literal("1.5e-3");
    assert(f3.is_float());

    // 5. Quoted string literals (strips outer quotes)
    SqliteValueOwned sq1 = SqliteValueOwned::from_literal("'hello world'");
    assert(sq1.is_text() && sq1.as_text() == SqliteStringView("hello world"));

    SqliteValueOwned sq2 = SqliteValueOwned::from_literal("\"database_name\"");
    assert(sq2.is_text() && sq2.as_text() == SqliteStringView("database_name"));

    // 6. Unquoted text / identifiers
    SqliteValueOwned t1 = SqliteValueOwned::from_literal("strict");
    assert(t1.is_text() && t1.as_text() == SqliteStringView("strict"));

    SqliteValueOwned t2 = SqliteValueOwned::from_literal("fast_mode_v2");
    assert(t2.is_text() && t2.as_text() == SqliteStringView("fast_mode_v2"));
}

void test_sbo_boundary_and_heap_transitions() {
    // String SBO exact boundary (13 chars vs 14 chars)
    const char* str13 = "1234567890123";
    SqliteValueOwned s13 = SqliteValueOwned::from_text(str13);
    assert(!s13.is_heap_allocated());
    assert(s13.inline_length() == 13);
    assert(s13.tag().len() == 13);
    assert(!s13.tag().is_heap());
    assert(s13.as_text() == str13);

    const char* str15 = "123456789012345";
    SqliteValueOwned s15 = SqliteValueOwned::from_text(str15);
    assert(s15.is_heap_allocated());
    assert(s15.tag().is_heap());

    // Blob SBO exact boundary (14 bytes vs 15 bytes)
    uint8_t blob14[14];
    memset(blob14, 0xAA, 14);
    SqliteValueOwned b14 = SqliteValueOwned::from_blob(blob14, 14);
    assert(!b14.is_heap_allocated());
    assert(b14.inline_length() == 14);
    assert(b14.tag().len() == 14);
    assert(!b14.tag().is_heap());
    assert(b14.as_blob().size() == 14);

    uint8_t blob15[15];
    memset(blob15, 0xBB, 15);
    SqliteValueOwned b15 = SqliteValueOwned::from_blob(blob15, 15);
    assert(b15.is_heap_allocated());
    assert(b15.tag().is_heap());

    // Empty string (0 bytes)
    SqliteValueOwned s0 = SqliteValueOwned::from_text("");
    assert(!s0.is_heap_allocated());
    assert(s0.inline_length() == 0);
    assert(s0.as_text().length() == 0);
    assert(s0.as_text() == "");
}

void test_owned_move_and_self_assign_safety(sqlite3* db) {
    // 1. Move inline text
    SqliteValueOwned inline_src = SqliteValueOwned::from_text("small text");
    assert(inline_src.is_valid());
    SqliteValueOwned inline_dst = sqlite_move(inline_src);
    assert(inline_dst.as_text() == "small text");
    assert(!inline_dst.is_heap_allocated());
    assert(inline_src.is_null()); // moved-from reset to NULL

    // 2. Move heap text from SQLite statement
    sqlite3_stmt* stmt = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT 'very long string that exceeds 14 bytes limit';", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    SqliteValueOwned heap_src(sqlite3_column_value(stmt, 0));
    assert(heap_src.is_heap_allocated());
    assert(heap_src.is_valid());
    
    SqliteValueOwned heap_dst = sqlite_move(heap_src);
    assert(heap_dst.is_heap_allocated());
    assert(heap_dst.is_valid());
    assert(heap_src.is_null()); // moved-from reset to NULL
    assert(heap_dst.as_text() == "very long string that exceeds 14 bytes limit");

    // 3. Self-move assignment
    heap_dst = sqlite_move(heap_dst);
    assert(heap_dst.is_valid());
    assert(heap_dst.as_text() == "very long string that exceeds 14 bytes limit");

    sqlite3_finalize(stmt);
}

void test_owned_value_arrays() {
    // 1. Static Key Tuple (Stack, 0 heap allocations)
    static_assert(sizeof(SqliteValueTuple<3>) == 48, "SqliteValueTuple<3> must be exactly 48 bytes!");
    static_assert(sizeof(SqliteValueTuple<4>) == 64, "SqliteValueTuple<4> must be exactly 64 bytes!");

    SqliteValueTuple<3> static_tuple;
    assert(static_tuple.size() == 3);
    assert(static_tuple.count() == 3);
    assert(!static_tuple.empty());
    assert(static_tuple[0].is_null());

    static_tuple[0] = SqliteValueOwned(500LL);
    static_tuple[1] = SqliteValueOwned::from_text("static_test");
    static_tuple[2] = SqliteValueOwned(3.14159);

    assert(static_tuple.as_int64(0) == 500);
    assert(static_tuple.as_text(1) == "static_test");
    assert(static_tuple.as_double(2) == 3.14159);
    assert(static_tuple.type(0) == SQLITE_INTEGER);
    assert(static_tuple.type(1) == SQLITE_TEXT);
    assert(static_tuple.type(2) == SQLITE_FLOAT);

    // 2. Adaptive Vector (SBO Stack + Heap Spill)
    SqliteValueVec<4> dyn_vec;
    dyn_vec.resize(3);
    assert(dyn_vec.size() == 3);
    assert(!dyn_vec.empty());
    assert(dyn_vec.is_inline());

    dyn_vec[0] = SqliteValueOwned(100LL);
    dyn_vec[1] = SqliteValueOwned::from_text("dyn_test");
    dyn_vec[2] = SqliteValueOwned(99.9);

    assert(dyn_vec.as_int64(0) == 100);
    assert(dyn_vec.as_text(1) == "dyn_test");
    assert(dyn_vec.as_double(2) == 99.9);

    // Copy Constructor
    SqliteValueVec<4> dyn_copy = dyn_vec;
    assert(dyn_copy.size() == 3);
    assert(dyn_copy.as_int64(0) == 100);
    assert(dyn_copy.as_text(1) == "dyn_test");

    // Move Constructor
    SqliteValueVec<4> dyn_moved = sqlite_move(dyn_vec);
    assert(dyn_moved.size() == 3);
    assert(dyn_moved.as_int64(0) == 100);
    assert(dyn_vec.empty());
    assert(dyn_vec.size() == 0);

    // Dynamic Resize (Grow & Spilling to Heap)
    dyn_moved.resize(5);
    assert(dyn_moved.size() == 5);
    assert(!dyn_moved.is_inline());
    assert(dyn_moved.as_int64(0) == 100);
    assert(dyn_moved.is_null(3));
    assert(dyn_moved.is_null(4));

    // Dynamic Shrink back to Stack
    dyn_moved.resize(2);
    assert(dyn_moved.size() == 2);
    assert(dyn_moved.is_inline());
    assert(dyn_moved.as_int64(0) == 100);
    assert(dyn_moved.as_text(1) == "dyn_test");

    // 3. Test SQLITE_DERIVE_ARRAY_HASH across containers
    assert(static_tuple.hash() != 0);
    assert(dyn_copy.hash() != 0);
}

// ============================================================================
// NEW TESTS FOR UPDATED sqlite3_value.hpp
// ============================================================================

/**
 * @brief Tests all new integer-family constructors:
 *        long, unsigned int, unsigned long, unsigned long long, bool.
 */
void test_new_integer_constructors() {
    // long
    SqliteValueOwned vl(42L);
    assert(vl.type() == SQLITE_INTEGER);
    assert(vl.as_int64() == 42);
    assert(vl.affinity() == SQLITE_AFF_INTEGER);
    assert(!vl.is_heap_allocated());

    // unsigned int
    SqliteValueOwned vu(100u);
    assert(vu.type() == SQLITE_INTEGER && vu.as_int64() == 100);

    // unsigned long
    SqliteValueOwned vul(200UL);
    assert(vul.type() == SQLITE_INTEGER && vul.as_int64() == 200);

    // unsigned long long
    SqliteValueOwned vull(9999999999ULL);
    assert(vull.type() == SQLITE_INTEGER && vull.as_int64() == 9999999999LL);

    // bool ctor: tagged with SQLITE_SUBTYPE_BOOL
    SqliteValueOwned b_true(true);
    assert(b_true.type() == SQLITE_INTEGER);
    assert(b_true.as_int64() == 1);
    assert(b_true.subtype() == SQLITE_SUBTYPE_BOOL);
    assert(b_true.is_bool());
    assert(b_true.as_bool());

    SqliteValueOwned b_false(false);
    assert(b_false.type() == SQLITE_INTEGER);
    assert(b_false.as_int64() == 0);
    assert(b_false.subtype() == SQLITE_SUBTYPE_BOOL);
    assert(b_false.is_bool());
    assert(!b_false.as_bool());

    // Heterogeneous comparisons work for all widths
    assert(vl  == 42);
    assert(vu  == 100u);
    assert(vul == 200UL);
    assert(vull == 9999999999LL);
    assert(b_true  == 1LL);
    assert(b_false == 0LL);

    // All non-heap, confirmed
    assert(!vu.is_heap_allocated());
    assert(!vul.is_heap_allocated());
    assert(!vull.is_heap_allocated());
    assert(!b_true.is_heap_allocated());
    assert(!b_false.is_heap_allocated());
}

/**
 * @brief Tests SqliteStringView and SqliteBlobView direct constructors
 *        including inline / heap boundary and optional subtype parameter.
 */
void test_string_blob_view_constructors() {
    // --- Inline text (5 chars, <= 13) ---
    SqliteStringView sv_short("hello", 5);
    SqliteValueOwned from_sv_short(sv_short);
    assert(from_sv_short.type() == SQLITE_TEXT);
    assert(!from_sv_short.is_heap_allocated());
    assert(from_sv_short.as_text() == sv_short);
    assert(from_sv_short.as_text().length() == 5);

    // --- Heap text (37 chars, > 13) ---
    const char* long_text = "this is a long string beyond 13 chars";
    SqliteStringView sv_long(long_text, 37);
    SqliteValueOwned from_sv_long(sv_long);
    assert(from_sv_long.type() == SQLITE_TEXT);
    assert(from_sv_long.is_heap_allocated());
    assert(from_sv_long.as_text() == sv_long);

    // --- Inline blob (3 bytes, <= 14) ---
    char blob_data[] = {0x01, 0x02, 0x03};
    SqliteBlobView bv_short(blob_data, 3);
    SqliteValueOwned from_bv_short(bv_short);
    assert(from_bv_short.type() == SQLITE_BLOB);
    assert(!from_bv_short.is_heap_allocated());
    assert(from_bv_short.as_blob() == bv_short);
    assert(from_bv_short.as_blob().size() == 3);

    // --- Heap blob (20 bytes, > 14) ---
    uint8_t big_blob[20];
    memset(big_blob, 0xCC, 20);
    SqliteBlobView bv_big(big_blob, 20);
    SqliteValueOwned from_bv_big(bv_big);
    assert(from_bv_big.type() == SQLITE_BLOB);
    assert(from_bv_big.is_heap_allocated());
    assert(from_bv_big.as_blob() == bv_big);

    // --- SqliteStringView ctor with explicit subtype ---
    SqliteValueOwned tagged_sv(sv_short, SQLITE_SUBTYPE_JSON);
    assert(tagged_sv.type() == SQLITE_TEXT);
    assert(tagged_sv.subtype() == SQLITE_SUBTYPE_JSON);
    assert(tagged_sv.is_json());
    assert(!tagged_sv.is_heap_allocated());

    // --- SqliteBlobView ctor with explicit subtype ---
    SqliteValueOwned tagged_bv(bv_short, SQLITE_SUBTYPE_UUID);
    assert(tagged_bv.type() == SQLITE_BLOB);
    assert(tagged_bv.subtype() == SQLITE_SUBTYPE_UUID);
    assert(tagged_bv.is_uuid());
}

/**
 * @brief Tests every operator= overload, including lifecycle safety
 *        (heap → free → reuse) verified by AddressSanitizer.
 */
void test_assignment_operators() {
    SqliteValueOwned val;
    assert(val.is_null());

    // int
    val = 42;
    assert(val.type() == SQLITE_INTEGER && val.as_int64() == 42);

    // long
    val = 100L;
    assert(val.type() == SQLITE_INTEGER && val.as_int64() == 100);

    // unsigned int
    val = 200u;
    assert(val.type() == SQLITE_INTEGER && val.as_int64() == 200);

    // unsigned long
    val = 300UL;
    assert(val.type() == SQLITE_INTEGER && val.as_int64() == 300);

    // unsigned long long
    val = 400ULL;
    assert(val.type() == SQLITE_INTEGER && val.as_int64() == 400);

    // bool (stores integer 0/1, no subtype on assignment path)
    val = true;
    assert(val.type() == SQLITE_INTEGER && val.as_int64() == 1);
    val = false;
    assert(val.type() == SQLITE_INTEGER && val.as_int64() == 0);

    // double
    val = 3.14;
    assert(val.type() == SQLITE_FLOAT && val.as_double() == 3.14);

    // float (promotes to double)
    val = 2.5f;
    assert(val.type() == SQLITE_FLOAT);

    // const char* (inline, <= 13)
    val = "hello";
    assert(val.type() == SQLITE_TEXT && !val.is_heap_allocated());
    assert(val.as_text() == "hello");

    // const char* (heap, > 13) — switches from inline to heap
    val = "this string is longer than thirteen chars";
    assert(val.type() == SQLITE_TEXT && val.is_heap_allocated());
    assert(val.as_text() == "this string is longer than thirteen chars");

    // Assign over heap → int: must free heap pointer without leak
    val = 99;
    assert(val.type() == SQLITE_INTEGER && val.as_int64() == 99);
    assert(!val.is_heap_allocated());

    // Re-allocate heap text, then assign from SqliteStringView (inline)
    val = "another string that exceeds thirteen chars!";
    assert(val.is_heap_allocated());
    SqliteStringView sv("world", 5);
    val = sv;
    assert(val.type() == SQLITE_TEXT && !val.is_heap_allocated());
    assert(val.as_text() == sv);

    // Assign from SqliteBlobView (inline)
    char bd[] = {0x10, 0x20};
    SqliteBlobView bv(bd, 2);
    val = bv;
    assert(val.type() == SQLITE_BLOB && !val.is_heap_allocated());
    assert(val.as_blob() == bv);

    // const char* nullptr → null
    val = (const char*)nullptr;
    assert(val.is_null());

    // Assign from heap string, then from double → heap must be freed
    val = "i am deliberately a long string above threshold";
    assert(val.is_heap_allocated());
    val = 1.618;
    assert(val.type() == SQLITE_FLOAT && !val.is_heap_allocated());
}

/**
 * @brief Tests static_null() singleton, static_null_array(), and is_active()
 *        for both SqliteValueOwned and SqliteOwnedValueTag.
 */
void test_static_null_invariants() {
    // static_null() returns a stable singleton reference
    const SqliteValueOwned& sn = SqliteValueOwned::static_null();
    assert(sn.is_null());
    assert(sn.type() == SQLITE_NULL);
    assert(sn.subtype() == SQLITE_SUBTYPE_NONE);
    assert(sn.affinity() == SQLITE_AFF_NONE);
    assert(sn.is_active());         // tag.raw >= 0x20 (SQLITE_NULL << 5 = 0xA0)
    assert(!sn.is_heap_allocated());
    assert(sn.is_valid());

    // Same address on repeated calls (singleton)
    const SqliteValueOwned& sn2 = SqliteValueOwned::static_null();
    assert(&sn == &sn2);

    // static_null_array() — 8 contiguous canonical NULLs
    const SqliteValueOwned* arr = SqliteValueOwned::static_null_array();
    assert(arr != nullptr);
    for (int i = 0; i < 8; ++i) {
        assert(arr[i].is_null());
        assert(arr[i].is_active());
        assert(arr[i].subtype() == SQLITE_SUBTYPE_NONE);
        assert(!arr[i].is_heap_allocated());
    }

    // Default-constructed value is also active (initialized with set_tag(NULL))
    SqliteValueOwned fresh;
    assert(fresh.is_active());
    assert(fresh.is_null());

    // SqliteOwnedValueTag direct is_active() threshold
    SqliteOwnedValueTag tag_zero;
    tag_zero.raw = 0x00;
    assert(!tag_zero.is_active());    // type == 0 → not active

    SqliteOwnedValueTag tag_int;
    tag_int.set(SQLITE_INTEGER, false, 0); // 0x20
    assert(tag_int.is_active());

    SqliteOwnedValueTag tag_null;
    tag_null.set(SQLITE_NULL, false, 0); // 0xA0
    assert(tag_null.is_active());
}

/**
 * @brief Tests set_null() lifecycle: proper heap free, inline reset, and idempotency.
 */
void test_set_null_lifecycle() {
    // 1. set_null on inline text (no heap, no free needed)
    SqliteValueOwned inline_val = SqliteValueOwned::from_text("hello");
    assert(!inline_val.is_heap_allocated());
    inline_val.set_null();
    assert(inline_val.is_null());
    assert(!inline_val.is_heap_allocated());

    // 2. set_null on heap text (must free without sanitizer error)
    SqliteValueOwned heap_val = SqliteValueOwned::from_text("a string longer than thirteen bytes");
    assert(heap_val.is_heap_allocated());
    heap_val.set_null();
    assert(heap_val.is_null());
    assert(!heap_val.is_heap_allocated());

    // 3. set_null on heap blob
    uint8_t big_blob[20];
    memset(big_blob, 0xEE, 20);
    SqliteValueOwned heap_blob = SqliteValueOwned::from_blob(big_blob, 20);
    assert(heap_blob.is_heap_allocated());
    heap_blob.set_null();
    assert(heap_blob.is_null());

    // 4. set_null on primitive integer (no heap)
    SqliteValueOwned int_val(42LL);
    int_val.set_null();
    assert(int_val.is_null());
    assert(!int_val.is_heap_allocated());

    // 5. Double set_null is safe (idempotent)
    int_val.set_null();
    assert(int_val.is_null());
    assert(int_val.subtype() == SQLITE_SUBTYPE_NONE);
    assert(int_val.affinity() == SQLITE_AFF_NONE);
    assert(int_val.is_active()); // Still an active NULL
}

/**
 * @brief Tests clone() and copy constructor for independent deep copies
 *        of both inline and heap-allocated values.
 */
void test_clone_and_deep_copy() {
    // 1. Clone inline text → same content, same SBO flag
    SqliteValueOwned orig_inline = SqliteValueOwned::from_text("hello");
    SqliteValueOwned cloned_inline = orig_inline.clone();
    assert(cloned_inline == orig_inline);
    assert(!cloned_inline.is_heap_allocated());
    assert(cloned_inline.as_text() == orig_inline.as_text());

    // 2. Clone heap text → independent allocation
    SqliteValueOwned orig_heap = SqliteValueOwned::from_text("this is a string longer than 13 chars");
    SqliteValueOwned cloned_heap = orig_heap.clone();
    assert(cloned_heap == orig_heap);
    assert(cloned_heap.is_heap_allocated());
    // Different pointer — independent allocation
    assert(cloned_heap.heap_value() != orig_heap.heap_value());
    assert(cloned_heap.as_text() == orig_heap.as_text());

    // Nullify original, clone must survive independently
    orig_heap.set_null();
    assert(cloned_heap.is_text());
    assert(cloned_heap.as_text() == "this is a string longer than 13 chars");

    // 3. Clone heap blob → independent allocation
    uint8_t big_blob[20];
    for (int i = 0; i < 20; ++i) big_blob[i] = (uint8_t)i;
    SqliteValueOwned orig_blob = SqliteValueOwned::from_blob(big_blob, 20);
    SqliteValueOwned cloned_blob = orig_blob.clone();
    assert(cloned_blob == orig_blob);
    assert(cloned_blob.is_heap_allocated());
    assert(cloned_blob.heap_value() != orig_blob.heap_value());
    assert(memcmp(cloned_blob.as_blob().data(), big_blob, 20) == 0);

    // 4. Copy constructor preserves subtype
    SqliteValueOwned orig_sub = SqliteValueOwned::from_json("{\"key\":\"val\"}");
    SqliteValueOwned copy_sub(orig_sub);
    assert(copy_sub.is_json());
    assert(copy_sub.subtype() == SQLITE_SUBTYPE_JSON);
    assert(copy_sub.as_text() == orig_sub.as_text());

    // 5. Copy constructor for 14-byte inline blob (maximum SBO)
    uint8_t blob14[14];
    memset(blob14, 0x7F, 14);
    SqliteValueOwned orig_inl_blob = SqliteValueOwned::from_blob(blob14, 14);
    SqliteValueOwned copy_inl_blob(orig_inl_blob);
    assert(!copy_inl_blob.is_heap_allocated());
    assert(copy_inl_blob.as_blob().size() == 14);
    assert(copy_inl_blob == orig_inl_blob);
}

/**
 * @brief Tests from_literal edge cases not covered by the existing test_from_literal():
 *        positive prefix numbers, empty quoted strings, mixed-case booleans,
 *        sized overload, partial number strings, SqliteStringView overload.
 */
void test_from_literal_edge_cases() {
    // 1. Positive-prefix integer (+5 → INTEGER 5)
    //    from_literal logic: p[0]=='+' → is_num=true → sscanf → 5
    SqliteValueOwned p1 = SqliteValueOwned::from_literal("+5");
    assert(p1.is_integer());
    assert(p1.as_int64() == 5);

    // 2. Positive-prefix float (+3.14 → FLOAT)
    SqliteValueOwned p2 = SqliteValueOwned::from_literal("+3.14");
    assert(p2.is_float());

    // 3. Empty single-quoted string → TEXT with length 0
    SqliteValueOwned eq1 = SqliteValueOwned::from_literal("''");
    assert(eq1.is_text());
    assert(eq1.as_text().length() == 0);

    // 4. Empty double-quoted string → TEXT with length 0
    SqliteValueOwned eq2 = SqliteValueOwned::from_literal("\"\"");
    assert(eq2.is_text());
    assert(eq2.as_text().length() == 0);

    // 5. Mixed-case booleans (case-insensitive)
    SqliteValueOwned bc1 = SqliteValueOwned::from_literal("TRUE");
    assert(bc1.is_bool() && bc1.as_bool());

    SqliteValueOwned bc2 = SqliteValueOwned::from_literal("FALSE");
    assert(bc2.is_bool() && !bc2.as_bool());

    SqliteValueOwned bc3 = SqliteValueOwned::from_literal("YES");
    assert(bc3.is_bool() && bc3.as_bool());

    SqliteValueOwned bc4 = SqliteValueOwned::from_literal("No");
    assert(bc4.is_bool() && !bc4.as_bool());

    SqliteValueOwned bc5 = SqliteValueOwned::from_literal("ON");
    assert(bc5.is_bool() && bc5.as_bool());

    SqliteValueOwned bc6 = SqliteValueOwned::from_literal("OFF");
    assert(bc6.is_bool() && !bc6.as_bool());

    // 6. Partial number string ("3abc") → falls through to TEXT
    SqliteValueOwned nt1 = SqliteValueOwned::from_literal("3abc");
    assert(nt1.is_text());

    // 7. Lone sign → TEXT (not a number)
    SqliteValueOwned lone_plus = SqliteValueOwned::from_literal("+");
    // is_num = true (p[0]=='+'), but loop at i=1 finds no digits → is_num stays...
    // Actually: len==1, loop never runs (i=1 not < 1), so is_num stays true.
    // sscanf("+", "%lld") returns 0 → condition (==1) fails → falls to float check
    // sscanf("+", "%lf") also returns 0 → falls to TEXT
    assert(lone_plus.is_text());

    // 8. Scientific notation
    SqliteValueOwned sci1 = SqliteValueOwned::from_literal("1e10");
    assert(sci1.is_float());

    SqliteValueOwned sci2 = SqliteValueOwned::from_literal("2.5E-3");
    assert(sci2.is_float());

    // 9. Sized overload (const char*, int len)
    SqliteValueOwned sized_null = SqliteValueOwned::from_literal("null", 4);
    assert(sized_null.is_null());

    SqliteValueOwned sized_bool = SqliteValueOwned::from_literal("true", 4);
    assert(sized_bool.is_bool() && sized_bool.as_bool());

    // Partial length: only "tru" from "true_extra" → falls to TEXT
    SqliteValueOwned partial = SqliteValueOwned::from_literal("true_extra", 3);
    assert(partial.is_text()); // "tru" is not "true"

    // 10. SqliteStringView overload
    SqliteStringView sv_false("false", 5);
    SqliteValueOwned from_sv_lit = SqliteValueOwned::from_literal(sv_false);
    assert(from_sv_lit.is_bool() && !from_sv_lit.as_bool());

    SqliteStringView sv_int("1024", 4);
    SqliteValueOwned from_sv_int = SqliteValueOwned::from_literal(sv_int);
    assert(from_sv_int.is_integer() && from_sv_int.as_int64() == 1024);

    // 11. NULL (uppercase) via SqliteStringView
    SqliteStringView sv_null("NULL", 4);
    SqliteValueOwned from_sv_null = SqliteValueOwned::from_literal(sv_null);
    assert(from_sv_null.is_null());
}

/**
 * @brief Tests from_uuid text overload and factory heap paths for
 *        from_decimal, from_json, from_jsonb, from_vector that exceed SBO limits.
 */
void test_subtype_factory_heap_paths() {
    // 1. from_uuid text variant: 36-char UUID string → heap TEXT
    const char* uuid_str = "550e8400-e29b-41d4-a716-446655440000";
    int uuid_len = (int)strlen(uuid_str); // 36
    SqliteValueOwned uuid_text = SqliteValueOwned::from_uuid(uuid_str);
    assert(uuid_text.is_uuid());
    assert(uuid_text.is_text());
    assert(uuid_text.subtype() == SQLITE_SUBTYPE_UUID);
    assert(uuid_text.is_heap_allocated()); // 36 > 13
    assert(uuid_text.as_text() == SqliteStringView(uuid_str, uuid_len));

    // 2. from_uuid binary (16 bytes) → heap BLOB (16 > 14 = MAX_INLINE_BLOB_LEN)
    const uint8_t uuid_bin[16] = {
        0x55, 0x0e, 0x84, 0x00, 0xe2, 0x9b, 0x41, 0xd4,
        0xa7, 0x16, 0x44, 0x66, 0x55, 0x44, 0x00, 0x00
    };
    SqliteValueOwned uuid_blob = SqliteValueOwned::from_uuid(uuid_bin);
    assert(uuid_blob.is_uuid());
    assert(uuid_blob.is_blob());
    assert(uuid_blob.is_heap_allocated()); // 16 > MAX_INLINE_BLOB_LEN (14)
    assert(uuid_blob.as_blob().size() == 16);
    assert(memcmp(uuid_blob.as_blob().data(), uuid_bin, 16) == 0);

    // 3. from_decimal heap path (> 13 chars)
    const char* big_dec = "12345678901234567890.9999999";
    int big_dec_len = (int)strlen(big_dec);
    SqliteValueOwned dec_heap = SqliteValueOwned::from_decimal(big_dec);
    assert(dec_heap.is_decimal());
    assert(dec_heap.is_text());
    assert(dec_heap.is_heap_allocated()); // > 13 chars
    assert(dec_heap.as_text() == SqliteStringView(big_dec, big_dec_len));

    // 4. from_json heap path (> 13 chars)
    const char* big_json = "{\"status\":\"ok\",\"code\":200}";
    int big_json_len = (int)strlen(big_json);
    SqliteValueOwned json_heap = SqliteValueOwned::from_json(big_json);
    assert(json_heap.is_json());
    assert(json_heap.is_text());
    assert(json_heap.is_heap_allocated());
    assert(json_heap.as_text() == SqliteStringView(big_json, big_json_len));

    // 5. from_jsonb heap path (> 14 bytes)
    const uint8_t jsonb[20] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x10, 0x11, 0x12, 0x13
    };
    SqliteValueOwned jsonb_heap = SqliteValueOwned::from_jsonb(jsonb, 20);
    assert(jsonb_heap.is_json());
    assert(jsonb_heap.is_blob());
    assert(jsonb_heap.is_heap_allocated());
    assert(jsonb_heap.as_blob().size() == 20);

    // 6. from_vector heap path (8 floats = 32 bytes > 14)
    const float vec[8] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};
    SqliteValueOwned vec_val = SqliteValueOwned::from_vector(vec, (int)sizeof(vec));
    assert(vec_val.is_vector());
    assert(vec_val.is_blob());
    assert(vec_val.is_heap_allocated()); // 32 bytes > 14
    assert(vec_val.as_blob().size() == (int)sizeof(vec));

    // 7. from_geometry inline path (8 bytes <= 14)
    const uint8_t geo_small[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    SqliteValueOwned geo_inline = SqliteValueOwned::from_geometry(geo_small, 8);
    assert(geo_inline.is_geometry());
    assert(!geo_inline.is_heap_allocated());
    assert(geo_inline.as_blob().size() == 8);

    // 8. from_compressed heap path (20 bytes > 14)
    const uint8_t compressed[20] = {
        0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8,
        0xF7, 0xF6, 0xF5, 0xF4, 0xF3, 0xF2, 0xF1, 0xF0,
        0xEF, 0xEE, 0xED, 0xEC
    };
    SqliteValueOwned comp_heap = SqliteValueOwned::from_compressed(compressed, 20);
    assert(comp_heap.is_compressed());
    assert(comp_heap.is_blob());
    assert(comp_heap.is_heap_allocated());
    assert(comp_heap.as_blob().size() == 20);
}

/**
 * @brief Tests explicit operator bool() (is_valid && !is_null)
 *        and as_bool() (integer payload != 0) on owned values.
 */
void test_bool_conversion_and_predicates() {
    // NULL: operator bool → false (is_null), as_bool → false
    SqliteValueOwned null_val;
    assert(!static_cast<bool>(null_val));
    assert(!null_val.as_bool());
    assert(null_val.is_active()); // properly initialized NULL IS active

    // INTEGER 0: not null → operator bool = true; as_bool = false
    SqliteValueOwned zero_val(0LL);
    assert(static_cast<bool>(zero_val)); // is_valid() && !is_null()
    assert(!zero_val.as_bool());         // payload == 0

    // INTEGER 1: both truthy
    SqliteValueOwned one_val(1LL);
    assert(static_cast<bool>(one_val));
    assert(one_val.as_bool());

    // INTEGER non-zero: as_bool → true
    SqliteValueOwned int_nonzero(7LL);
    assert(int_nonzero.as_bool());

    // FLOAT 0.0: not null → truthy via operator bool
    SqliteValueOwned float_zero(0.0);
    assert(static_cast<bool>(float_zero));
    // as_bool is only meaningful for INTEGER; float returns 0 via as_int64()
    assert(!float_zero.as_bool()); // as_int64() returns integer bits interpreted, effectively 0

    // TEXT: operator bool → true
    SqliteValueOwned text_val("hello");
    assert(static_cast<bool>(text_val));

    // BLOB: operator bool → true
    char bd[] = {0x01};
    SqliteValueOwned blob_val(SqliteBlobView(bd, 1));
    assert(static_cast<bool>(blob_val));

    // is_active() must be true for all initialized values
    assert(null_val.is_active());
    assert(zero_val.is_active());
    assert(one_val.is_active());
    assert(text_val.is_active());
    assert(blob_val.is_active());

    // Type predicates exhaustive coverage
    assert(null_val.is_null());
    assert(!null_val.is_integer());
    assert(!null_val.is_float());
    assert(!null_val.is_text());
    assert(!null_val.is_blob());
    assert(!null_val.is_numeric());

    assert(zero_val.is_integer());
    assert(zero_val.is_numeric());

    assert(float_zero.is_float());
    assert(float_zero.is_numeric());

    assert(text_val.is_text());
    assert(!text_val.is_numeric());

    assert(blob_val.is_blob());
    assert(!blob_val.is_numeric());
}

/**
 * @brief Tests copy assignment operator=() (deep copy) for all representations.
 */
void test_copy_assignment() {
    // Inline text → inline text
    SqliteValueOwned src_inline = SqliteValueOwned::from_text("hello");
    SqliteValueOwned dst;
    dst = src_inline;
    assert(dst == src_inline);
    assert(!dst.is_heap_allocated());

    // Heap text → heap text (deep copy, independent)
    SqliteValueOwned src_heap = SqliteValueOwned::from_text("long string that exceeds 13 chars exactly");
    dst = src_heap;
    assert(dst == src_heap);
    assert(dst.is_heap_allocated());
    assert(dst.heap_value() != src_heap.heap_value()); // independent allocation

    // Mutate src, dst must be unaffected
    src_heap.set_null();
    assert(dst.is_text());

    // Self-assignment (must be safe)
    dst = dst;
    assert(dst.is_text());
    assert(dst.as_text() == "long string that exceeds 13 chars exactly");

    // Heap blob → heap blob
    uint8_t big_blob[20];
    memset(big_blob, 0xAB, 20);
    SqliteValueOwned src_blob = SqliteValueOwned::from_blob(big_blob, 20);
    dst = src_blob;
    assert(dst == src_blob);
    assert(dst.is_heap_allocated());

    // Integer copy
    SqliteValueOwned src_int(123LL);
    dst = src_int;
    assert(dst.is_integer() && dst.as_int64() == 123);
    assert(!dst.is_heap_allocated());

    // NULL copy
    SqliteValueOwned src_null;
    dst = src_null;
    assert(dst.is_null());
}

void test_value_array_iterators(sqlite3* db) {
    // 1. SqliteValueTuple<3> Iterator
    SqliteValueTuple<3> s_tuple;
    s_tuple[0] = SqliteValueOwned(10LL);
    s_tuple[1] = SqliteValueOwned(20LL);
    s_tuple[2] = SqliteValueOwned(30LL);

    sqlite3_int64 s_sum = 0;
    int s_count = 0;
    for (const SqliteValueOwned& val : s_tuple) {
        s_sum += val.as_int64();
        s_count++;
    }
    assert(s_count == 3);
    assert(s_sum == 60);

    // 2. SqliteValueVec<4> Iterator
    SqliteValueVec<4> d_vec;
    d_vec.resize(3);
    d_vec[0] = SqliteValueOwned::from_text("alpha");
    d_vec[1] = SqliteValueOwned::from_text("beta");
    d_vec[2] = SqliteValueOwned::from_text("gamma");

    int d_count = 0;
    for (const SqliteValueOwned& val : d_vec) {
        assert(val.type() == SQLITE_TEXT);
        d_count++;
    }
    assert(d_count == 3);

    // 3. SqliteRowView Iterator (via statement row)
    sqlite3_stmt* stmt = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT 111, 'text_val', 3.14;", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);

    SqliteRowView r_view(stmt);
    assert(r_view.size() == 3);
    int v_count = 0;
    for (SqliteValueView val : r_view) {
        assert(!val.is_null());
        v_count++;
    }
    assert(v_count == 3);

    // 4. Empty Container Iterator
    SqliteValueVec<4> empty_vec(0);
    int empty_count = 0;
    for (const SqliteValueOwned& val : empty_vec) {
        (void)val;
        empty_count++;
    }
    assert(empty_count == 0);
    assert(empty_vec.begin() == empty_vec.end());

    sqlite3_finalize(stmt);
}

void test_subtag_and_immutability() {
    // 1. SqliteOwnedValueSubTag bit operations unit tests
    SqliteOwnedValueSubTag st;
    st.clear();
    assert(st.raw == 0);
    assert(st.subtype() == SQLITE_SUBTYPE_NONE);
    assert(!st.is_immutable());

    st.set(SQLITE_SUBTYPE_JSON, false);
    assert(st.subtype() == SQLITE_SUBTYPE_JSON);
    assert(!st.is_immutable());

    st.mark_immutable();
    assert(st.is_immutable());
    assert(st.subtype() == SQLITE_SUBTYPE_JSON);
    assert(st.raw == (SQLITE_SUBTYPE_JSON | 0x80));

    st.unmark_immutable();
    assert(!st.is_immutable());
    assert(st.subtype() == SQLITE_SUBTYPE_JSON);

    st.set_subtype(SQLITE_SUBTYPE_DECIMAL);
    assert(st.subtype() == SQLITE_SUBTYPE_DECIMAL);
    assert(!st.is_immutable());

    st.set_immutable(true);
    assert(st.is_immutable());
    assert(st.subtype() == SQLITE_SUBTYPE_DECIMAL);

    // 2. Constructors with is_immutable
    SqliteValueOwned imm_null(nullptr, true);
    assert(imm_null.is_null());
    assert(imm_null.is_immutable());

    SqliteValueOwned imm_i64(42LL, SQLITE_SUBTYPE_NONE, SQLITE_AFF_INTEGER, true);
    assert(imm_i64.is_integer());
    assert(imm_i64.as_int64() == 42LL);
    assert(imm_i64.is_immutable());

    SqliteValueOwned imm_int(100, SQLITE_SUBTYPE_NONE, SQLITE_AFF_INTEGER, true);
    assert(imm_int.as_int() == 100);
    assert(imm_int.is_immutable());

    SqliteValueOwned imm_dbl(3.14159, SQLITE_SUBTYPE_NONE, SQLITE_AFF_REAL, true);
    assert(imm_dbl.is_float());
    assert(imm_dbl.as_double() == 3.14159);
    assert(imm_dbl.is_immutable());

    SqliteValueOwned imm_bool(true, SQLITE_SUBTYPE_BOOL, SQLITE_AFF_INTEGER, true);
    assert(imm_bool.as_bool());
    assert(imm_bool.is_immutable());
    assert(imm_bool.subtype() == SQLITE_SUBTYPE_BOOL);

    SqliteValueOwned imm_str("hello_immutable", SQLITE_SUBTYPE_NONE, true);
    assert(imm_str.is_text());
    assert(imm_str.as_text() == "hello_immutable");
    assert(imm_str.is_immutable());

    const char* big_str = "a very long immutable string that is heap allocated";
    SqliteValueOwned imm_heap_str(big_str, SQLITE_SUBTYPE_NONE, true);
    assert(imm_heap_str.is_text());
    assert(imm_heap_str.is_heap_allocated());
    assert(imm_heap_str.as_text() == big_str);
    assert(imm_heap_str.is_immutable());

    SqliteValueOwned imm_copy(imm_heap_str, false); // explicit override to mutable
    assert(!imm_copy.is_immutable());
    assert(imm_copy.as_text() == big_str);

    SqliteValueOwned imm_fact = SqliteValueOwned::from_text("factory_text", -1, SQLITE_SUBTYPE_NONE, true);
    assert(imm_fact.is_immutable());
    assert(imm_fact.as_text() == "factory_text");

    SqliteValueOwned imm_lit = SqliteValueOwned::from_literal(SqliteStringView("9999"), true);
    assert(imm_lit.is_immutable());
    assert(imm_lit.as_int64() == 9999);

    // 3. Mutator rejection on immutable value (must be safe no-ops)
    imm_i64 = 999LL;
    assert(imm_i64.as_int64() == 42LL); // unchanged!

    imm_i64 = 2.718;
    assert(imm_i64.as_int64() == 42LL); // unchanged!

    imm_i64 = "new text";
    assert(imm_i64.as_int64() == 42LL); // unchanged!

    imm_i64 = SqliteStringView("strview");
    assert(imm_i64.as_int64() == 42LL); // unchanged!

    imm_i64 = SqliteBlobView("blob", 4);
    assert(imm_i64.as_int64() == 42LL); // unchanged!

    imm_i64.set_integer(500);
    assert(imm_i64.as_int64() == 42LL); // unchanged!

    imm_i64.set_float(500.5);
    assert(imm_i64.as_int64() == 42LL); // unchanged!

    imm_i64.set_text("mutate");
    assert(imm_i64.as_int64() == 42LL); // unchanged!

    imm_i64.set_blob("mutate", 6);
    assert(imm_i64.as_int64() == 42LL); // unchanged!

    imm_i64.set_null();
    assert(imm_i64.as_int64() == 42LL); // unchanged!

    imm_i64.reset();
    assert(imm_i64.as_int64() == 42LL); // unchanged!

    imm_i64.clear();
    assert(imm_i64.as_int64() == 42LL); // unchanged!

    imm_i64.set_subtype(SQLITE_SUBTYPE_JSON);
    assert(imm_i64.subtype() == SQLITE_SUBTYPE_NONE); // unchanged!

    imm_i64.set_affinity('A');
    assert(imm_i64.affinity() == SQLITE_AFF_INTEGER); // unchanged!

    // 4. Copy and Move Assignment to immutable target
    SqliteValueOwned other(777LL);
    imm_i64 = other;
    assert(imm_i64.as_int64() == 42LL); // unchanged!

    imm_i64 = SqliteValueOwned(888LL);
    assert(imm_i64.as_int64() == 42LL); // unchanged!

    // 5. Move from immutable source: source must NOT be zeroed/stolen
    SqliteValueOwned stolen_dst = sqlite_move(imm_heap_str);
    assert(stolen_dst.as_text() == big_str);
    assert(imm_heap_str.as_text() == big_str); // unchanged!
    assert(imm_heap_str.is_immutable());

    // 6. make_immutable, as_immutable, clone_mutable
    SqliteValueOwned val(55LL);
    assert(!val.is_immutable());
    val.make_immutable();
    assert(val.is_immutable());
    val = 66LL;
    assert(val.as_int64() == 55LL); // protected!

    SqliteValueOwned mut_clone = val.clone_mutable();
    assert(!mut_clone.is_immutable());
    mut_clone = 66LL;
    assert(mut_clone.as_int64() == 66LL); // mutated successfully!

    SqliteValueOwned imm_clone = mut_clone.as_immutable();
    assert(imm_clone.is_immutable());
    imm_clone = 77LL;
    assert(imm_clone.as_int64() == 66LL); // protected!
}

int main() {
    sqlite3_initialize();
    
    sqlite3* db;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
        printf("Failed to open sqlite db\n");
        return 1;
    }

    printf("Testing String Types...\n");
    test_string_types(db);
    
    printf("Testing Blob Types...\n");
    test_blob_types();
    
    printf("Testing Value Types...\n");
    test_value_types(db);
    
    printf("Testing String Builder...\n");
    test_string_builder(db);
    
    printf("Testing Move Semantics...\n");
    test_move_semantics();
    
    printf("Testing Strict Equality...\n");
    test_value_strict_equality(db);
    
    printf("Testing Heterogeneous Lookups...\n");
    test_heterogeneous_lookups(db);
    
    printf("Testing Coverage Edge Cases...\n");
    test_coverage_edge_cases(db);
    
    printf("Testing View Heterogeneous Lookups...\n");
    test_view_heterogeneous_lookups(db);
    
    printf("Testing Relational Operators...\n");
    test_relational_operators(db);
    test_cross_type_relational_operators(db);
    test_exhaustive_value_relational_operators(db);
    
    printf("Testing Value Move Semantics...\n");
    test_value_move_semantics();
    
    printf("Testing Primitive Constructors...\n");
    test_primitive_constructors();
    
    printf("Testing Hashing Stability...\n");
    test_hashing();
    
    printf("Testing Collation Order...\n");
    test_collation(db);
    
    printf("Testing Bind Methods...\n");
    test_bind(db);
    
    printf("Testing Result Methods...\n");
    test_result(db);
    
    printf("Testing as_text and as_blob on SqliteValueView & SqliteValueOwned...\n");
    test_as_text_and_as_blob(db);

    printf("Testing Subtypes & Affinities (16-byte Layout)...\n");
    test_subtypes_and_affinities(db);
    
    printf("Testing Value View Ergonomics (to_owned, predicates, from_column)...\n");
    test_value_view_ergonomics(db);

    printf("Testing All Subtype Factories & Roundtrips...\n");
    test_all_subtype_factories(db);

    printf("Testing SqliteValueOwned::from_literal (SQL Literal Inferred Parsing)...\n");
    test_from_literal();

    printf("Testing SBO Boundary Exact Transitions (13 vs 14 chars, 14 vs 15 bytes)...\n");
    test_sbo_boundary_and_heap_transitions();

    printf("Testing Owned Move & Self-Assignment Lifetime Safety...\n");
    test_owned_move_and_self_assign_safety(db);

    printf("Testing SqliteValueOwned Arrays (Static, Dynamic, Unified)...\n");
    test_owned_value_arrays();

    printf("Testing SqliteValue Array Iterators (Range-Based Loops)...\n");
    test_value_array_iterators(db);

    // ================================================================
    // NEW TESTS FOR UPDATED sqlite3_value.hpp
    // ================================================================

    printf("Testing New Integer Constructors (long, uint, ulong, ull, bool)...\n");
    test_new_integer_constructors();

    printf("Testing SqliteStringView and SqliteBlobView Constructors...\n");
    test_string_blob_view_constructors();

    printf("Testing Assignment Operators (All Overloads with Heap Lifecycle)...\n");
    test_assignment_operators();

    printf("Testing static_null(), static_null_array(), and is_active()...\n");
    test_static_null_invariants();

    printf("Testing set_null() Lifecycle (Heap Free, Inline Reset, Idempotency)...\n");
    test_set_null_lifecycle();

    printf("Testing clone() and Deep Copy (Independent Heap Allocations)...\n");
    test_clone_and_deep_copy();

    printf("Testing from_literal() Edge Cases (+prefix, empty quotes, mixed-case bools, sized)...\n");
    test_from_literal_edge_cases();

    printf("Testing Subtype Factory Heap Paths (UUID text, decimal, json, jsonb, vector, geometry, compressed)...\n");
    test_subtype_factory_heap_paths();

    printf("Testing operator bool() and as_bool() Type Predicates...\n");
    test_bool_conversion_and_predicates();

    printf("Testing Copy Assignment Operator (Deep Clone, Self-Assign, Independence)...\n");
    test_copy_assignment();

    printf("Testing SqliteOwnedValueSubTag and Immutability (Constructors, Mutator Guards, Sinks)...\n");
    test_subtag_and_immutability();

    sqlite3_close(db);
    sqlite3_shutdown();

    printf("All C++ Type Tests Passed!\n");
    return 0;
}
