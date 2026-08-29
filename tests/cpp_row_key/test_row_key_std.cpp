#define SQLITE_CORE
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sqlite3.h>
#include <string>
#include <map>
#include <unordered_map>
#include "sqlite3_row.hpp"
#include "sqlite3_row_key.hpp"

void test_transparent_unordered_map_row_key() {
    printf("Running test_transparent_unordered_map_row_key...\n");

    // std::unordered_map with transparent SqliteRowKeyHash and SqliteRowKeyEqual
    std::unordered_map<SqliteRowKeyOwned, std::string, SqliteRowKeyHash, SqliteRowKeyEqual> hash_map;

    SqliteRowKeyOwned k_int(SqliteValueOwned(42));
    SqliteRowKeyOwned k_str(SqliteValueOwned("alice"));
    SqliteRowKeyOwned k_dbl(SqliteValueOwned(3.14));

    hash_map[k_int] = "User 42";
    hash_map[k_str] = "User Alice";
    hash_map[k_dbl] = "User Pi";

    assert(hash_map.size() == 3);

    // Standard lookup by key
    assert(hash_map.find(k_int) != hash_map.end());
    assert(hash_map.find(k_int)->second == "User 42");

    assert(hash_map.find(k_str) != hash_map.end());
    assert(hash_map.find(k_str)->second == "User Alice");
}

void test_transparent_ordered_map_row_key() {
    printf("Running test_transparent_ordered_map_row_key...\n");

    // std::map with transparent SqliteRowKeyLess (enables heterogeneous lookups in C++14+)
    std::map<SqliteRowKeyOwned, std::string, SqliteRowKeyLess> btree_idx;

    btree_idx[SqliteRowKeyOwned(SqliteValueOwned(10))] = "Record 10";
    btree_idx[SqliteRowKeyOwned(SqliteValueOwned(20))] = "Record 20";
    btree_idx[SqliteRowKeyOwned(SqliteValueOwned(30))] = "Record 30";
    btree_idx[SqliteRowKeyOwned(SqliteValueOwned("apple"))] = "Fruit Apple";
    btree_idx[SqliteRowKeyOwned(SqliteValueOwned("banana"))] = "Fruit Banana";

    // 1. Heterogeneous find using native int (zero key construction!)
    auto it1 = btree_idx.find(10);
    assert(it1 != btree_idx.end());
    assert(it1->second == "Record 10");

    auto it2 = btree_idx.find(20);
    assert(it2 != btree_idx.end());
    assert(it2->second == "Record 20");

    assert(btree_idx.find(99) == btree_idx.end());

    // 2. Heterogeneous find using const char* (zero allocation!)
    auto it3 = btree_idx.find("apple");
    assert(it3 != btree_idx.end());
    assert(it3->second == "Fruit Apple");

    assert(btree_idx.find("cherry") == btree_idx.end());

    // 3. Heterogeneous find using SqliteStringView
    SqliteStringView sv_banana("banana", 6);
    auto it4 = btree_idx.find(sv_banana);
    assert(it4 != btree_idx.end());
    assert(it4->second == "Fruit Banana");

    // 4. Heterogeneous find using SqliteRowOwnedWrapper
    SqliteValueOwned val_30(30);
    SqliteRowOwnedWrapper wrap_30(val_30);
    auto it5 = btree_idx.find(wrap_30);
    assert(it5 != btree_idx.end());
    assert(it5->second == "Record 30");

    // 5. Heterogeneous range queries (lower_bound / upper_bound)
    auto it_lb = btree_idx.lower_bound(15);
    assert(it_lb != btree_idx.end());
    assert(it_lb->second == "Record 20");
}

void test_transparent_ordered_map_row_wrapper() {
    printf("Running test_transparent_ordered_map_row_wrapper...\n");

    // std::map with transparent SqliteRowLess over transient row spans
    std::map<SqliteRowOwnedWrapper, int, SqliteRowLess> span_idx;

    SqliteValueOwned v1(100);
    SqliteValueOwned v2(200);
    SqliteRowOwnedWrapper wrap1(v1);
    SqliteRowOwnedWrapper wrap2(v2);

    span_idx[wrap1] = 1;
    span_idx[wrap2] = 2;

    // Heterogeneous find using native int
    auto it1 = span_idx.find(100);
    assert(it1 != span_idx.end());
    assert(it1->second == 1);

    auto it2 = span_idx.find(200);
    assert(it2 != span_idx.end());
    assert(it2->second == 2);

    assert(span_idx.find(300) == span_idx.end());
}

void test_composite_key_ordered_map() {
    printf("Running test_composite_key_ordered_map...\n");

    std::map<SqliteRowKeyOwned, std::string, SqliteRowKeyLess> composite_map;

    SqliteRowDynamic row1(2);
    row1[0] = SqliteValueOwned(10);
    row1[1] = SqliteValueOwned(1);
    int indices[] = {0, 1};
    SqliteRowKeyOwned k_10_1(row1, indices, 2);

    SqliteRowDynamic row2(2);
    row2[0] = SqliteValueOwned(10);
    row2[1] = SqliteValueOwned(2);
    SqliteRowKeyOwned k_10_2(row2, indices, 2);

    composite_map[k_10_1] = "Order 10-1";
    composite_map[k_10_2] = "Order 10-2";

    assert(composite_map.size() == 2);

    // Prefix search with single value 10: (10) < (10, 1)
    auto it_lb = composite_map.lower_bound(10);
    assert(it_lb != composite_map.end());
    assert(it_lb->second == "Order 10-1");
}

void test_transparent_map_with_row_view() {
    printf("Running test_transparent_map_with_row_view...\n");

    sqlite3* db = nullptr;
    assert(sqlite3_open(":memory:", &db) == SQLITE_OK);

    sqlite3_stmt* stmt = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT 42, 'alice', 99.5;", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);

    SqliteRowView row_view(stmt);
    assert(row_view.size() == 3);

    // 1. Transparent lookup in std::map<SqliteRowDynamic, ...> using SqliteRowView
    std::map<SqliteRowDynamic, std::string, SqliteRowLess> ordered_row_map;
    SqliteRowDynamic row_dyn = row_view.to_owned();
    ordered_row_map[sqlite_move(row_dyn)] = "Alice Record";

    auto it_map = ordered_row_map.find(row_view);
    assert(it_map != ordered_row_map.end());
    assert(it_map->second == "Alice Record");

    // 2. Transparent lookup in std::unordered_map<SqliteRowDynamic, ...> using SqliteRowView
    std::unordered_map<SqliteRowDynamic, std::string, SqliteRowHash, SqliteRowEqual> hash_row_map;
    SqliteRowDynamic row_dyn2 = row_view.to_owned();
    hash_row_map[sqlite_move(row_dyn2)] = "Alice Hash Record";

    // 3. Single-column row view lookup in single key map
    sqlite3_stmt* stmt1 = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT 42;", -1, &stmt1, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt1) == SQLITE_ROW);
    SqliteRowView row_view1(stmt1);

    std::map<SqliteRowKeyOwned, std::string, SqliteRowKeyLess> key_map;
    key_map[SqliteRowKeyOwned(SqliteValueOwned(42LL))] = "Primary Key 42";

    auto it_key = key_map.find(row_view1);
    assert(it_key != key_map.end());
    assert(it_key->second == "Primary Key 42");

    sqlite3_finalize(stmt1);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

int main() {
    printf("================================================================\n");
    printf("RUNNING SQLITE ROW KEY STD TESTS (Transparent B-Tree & Swiss Table)\n");
    printf("================================================================\n");
    test_transparent_unordered_map_row_key();
    test_transparent_ordered_map_row_key();
    test_transparent_ordered_map_row_wrapper();
    test_composite_key_ordered_map();
    test_transparent_map_with_row_view();
    printf("================================================================\n");
    printf("ALL SQLITE ROW KEY STD TESTS PASSED SUCCESSFULLY!\n");
    printf("================================================================\n");
    return 0;
}
