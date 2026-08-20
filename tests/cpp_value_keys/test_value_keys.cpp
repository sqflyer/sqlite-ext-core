#include <sqlite3.h>
#define SQLITE_CORE
#include "../../include/sqlite3_value_keys.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <cstring>
#include <map>

using namespace std;

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

int main() {
    sqlite3_initialize();
    
    sqlite3* db;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
        cerr << "Failed to open sqlite db" << endl;
        return 1;
    }

    cout << "Testing String Types..." << endl;
    test_string_types(db);
    
    cout << "Testing Blob Types..." << endl;
    test_blob_types();
    
    cout << "Testing Value Types..." << endl;
    test_value_types(db);
    
    sqlite3_close(db);
    sqlite3_shutdown();
    
    cout << "All C++ Type Tests Passed!" << endl;
    return 0;
}
