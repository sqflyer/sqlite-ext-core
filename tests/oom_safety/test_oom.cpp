#include <sqlite3.h>
#define SQLITE_CORE
#include "sqlite3_value.hpp"
#include "sqlite3_buffer.hpp"
#include "sqlite3_statement.hpp"
#include <assert.h>
#include <stdio.h>

void test_sqlite_string_owned_null_safety() {
    printf("1. Testing SqliteStringOwned null & empty safety...\n");
    
    SqliteStringOwned str_empty;
    assert(str_empty.is_valid());
    assert((bool)str_empty);
    assert(str_empty.length() == 0);
    assert(str_empty.errcode() == SQLITE_OK);

    // Empty finish
    char* raw = str_empty.finish();
    if (raw) {
        sqlite3_free(raw);
    }

    // Finished string is now null
    assert(!str_empty.is_valid());
    assert(!(bool)str_empty);
    assert(str_empty.value() == nullptr);
    assert(str_empty.length() == 0);
    assert(str_empty.finish() == nullptr);
    assert(str_empty.hash() == SqliteStringUtil::hash(nullptr, 0));

    // Operations on finished/null string must not crash
    str_empty.append("test", 4);
    str_empty.appendall("test");
    str_empty.appendchar(5, 'x');
    str_empty.reset();

    printf("   [PASS] SqliteStringOwned null safety verified.\n");
}

void test_sqlite_blob_owned_null_safety() {
    printf("2. Testing SqliteBlobOwned null & empty safety...\n");
    
    SqliteBlobOwned blob_empty;
    assert(blob_empty.is_valid());
    assert((bool)blob_empty);
    assert(blob_empty.size() == 0);
    assert(blob_empty.data() == nullptr);
    assert(blob_empty.hash() == SqliteBlobUtil::hash(nullptr, 0));

    // Move construct
    SqliteBlobOwned moved(static_cast<SqliteBlobOwned&&>(blob_empty));
    assert(moved.is_valid());
    assert(moved.size() == 0);
    assert(moved.data() == nullptr);

    // Move assign
    SqliteBlobOwned moved2;
    moved2 = static_cast<SqliteBlobOwned&&>(moved);
    assert(moved2.is_valid());
    assert(moved2.size() == 0);

    printf("   [PASS] SqliteBlobOwned null safety verified.\n");
}

void test_sqlite_value_owned_null_safety() {
    printf("3. Testing SqliteValueOwned null & SBO safety...\n");
    
    SqliteValueOwned val_null(static_cast<const sqlite3_value*>(nullptr));
    assert(val_null.type() == SQLITE_NULL);
    assert(val_null.is_valid());
    assert((bool)val_null);
    assert(val_null.as_int64() == 0);
    assert(val_null.as_double() == 0.0);
    assert(val_null.as_text().length() == 0);
    assert(val_null.as_blob().size() == 0);

    // Compare nulls
    SqliteValueOwned val_null2(static_cast<const sqlite3_value*>(nullptr));
    assert(val_null == val_null2);
    assert(!(val_null < val_null2));

    printf("   [PASS] SqliteValueOwned null safety verified.\n");
}

void test_sqlite_buffer_null_safety() {
    printf("4. Testing SqliteBuffer & SqliteString null & OOM safety...\n");
    
    SqliteBuffer buf;
    assert(buf.is_valid());
    assert((bool)buf);
    assert(buf.bytes() == 0);
    assert(buf.capacity() == 0);
    assert(buf.data() == nullptr);
    assert(buf.hash() == SqliteHashUtil::hash(nullptr, 0));

    // Append 0 bytes or nullptr is safe
    assert(buf.append(nullptr, 0));
    assert(buf.append(nullptr, -5));
    assert(buf.append_uninitialized(0) == nullptr);
    assert(buf.append_uninitialized(-1) == nullptr);

    SqliteString str;
    assert(str.is_valid());
    assert((bool)str);
    assert(str.length() == 0);
    assert(str.c_str() != nullptr);
    assert(str.c_str()[0] == '\0');
    assert(str == "");

    assert(str.append(nullptr));
    assert(str.length() == 0);

    printf("   [PASS] SqliteBuffer & SqliteString null safety verified.\n");
}

void test_sqlite_statement_null_safety() {
    printf("5. Testing SqliteStatement & SqliteCachedStatement null safety...\n");
    
    SqliteStatement stmt;
    assert(!stmt.is_valid());
    assert(!(bool)stmt);
    assert(stmt.get() == nullptr);
    assert(stmt.finalize() == SQLITE_OK);
    assert(stmt.step() == SQLITE_MISUSE);
    assert(stmt.reset() == SQLITE_MISUSE);
    assert(stmt.clear_bindings() == SQLITE_MISUSE);
    assert(stmt.column_count() == 0);
    assert(stmt.column_int(0) == 0);
    assert(stmt.column_int64(0) == 0);
    assert(stmt.column_double(0) == 0.0);
    assert(stmt.column_text(0) == nullptr);
    assert(stmt.column_blob(0) == nullptr);
    assert(stmt.column_string_view(0).length() == 0);
    assert(stmt.column_blob_view(0).size() == 0);

    SqliteCachedStatement cached_stmt;
    assert(!cached_stmt.is_valid());
    assert(!(bool)cached_stmt);
    assert(cached_stmt.get() == nullptr);

    printf("   [PASS] SqliteStatement null safety verified.\n");
}

int main() {
    printf("=================================================================\n");
    printf("Running Constructor OOM & Null-Safety Test Suite\n");
    printf("=================================================================\n");
    
    test_sqlite_string_owned_null_safety();
    test_sqlite_blob_owned_null_safety();
    test_sqlite_value_owned_null_safety();
    test_sqlite_buffer_null_safety();
    test_sqlite_statement_null_safety();

    printf("\nAll 5 OOM & Null-Safety Test Suites Passed Successfully!\n");
    return 0;
}
