#include <sqlite3.h>
#define SQLITE_CORE
#include "../../include/sqlite3_value.hpp"
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
    sqlite3_stmt* stmt;
    
    // Test Integer Value
    sqlite3_prepare_v2(db, "SELECT 42;", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    sqlite3_value* int_val = sqlite3_column_value(stmt, 0);
    
    SqliteValueView view_int(int_val);
    SqliteValueOwned owned_int(int_val);
    
    assert(view_int.type() == SQLITE_INTEGER);
    assert(owned_int.type() == SQLITE_INTEGER);
    assert(view_int == owned_int);
    assert(owned_int == view_int);
    assert(!(view_int != owned_int));
    assert(view_int.hash() == owned_int.hash());
    
    sqlite3_finalize(stmt);
    
    // Test Text Value
    sqlite3_prepare_v2(db, "SELECT 'hello';", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    sqlite3_value* text_val = sqlite3_column_value(stmt, 0);
    
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
    
    sqlite3_finalize(stmt);
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

    SqliteValueOwned moved_blob = static_cast<SqliteValueOwned&&>(owned_blob);
    assert(moved_blob.as_blob() == bv1);
    assert(owned_blob.as_blob().data() == nullptr);
    assert(owned_blob.as_blob().size() == 0);

    sqlite3_finalize(stmt);
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
    
    sqlite3_close(db);
    sqlite3_shutdown();
    
    printf("All C++ Type Tests Passed!\n");
    return 0;
}
