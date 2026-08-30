#define SQLITE_CORE
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sqlite3.h>
#include <string>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <utility>
#include <tuple>
#include "sqlite3_row.hpp"
#include "sqlite3_value_containers.hpp"

// ============================================================================
// 1. Transparent std::unordered_map & std::unordered_set with SqliteValueTuple
// ============================================================================
void test_transparent_unordered_map_tuple() {
    printf("1. Running test_transparent_unordered_map_tuple...\n");

    std::unordered_map<SqliteValueTuple<2>, std::string, SqliteRowHash, SqliteRowEqual> tuple_map;

    SqliteValueTuple<2> k1(101, "sensor_alpha");
    SqliteValueTuple<2> k2(102, "sensor_beta");

    tuple_map[k1] = "Alpha Station";
    tuple_map[k2] = "Beta Station";

    assert(tuple_map.size() == 2);
    assert(tuple_map.find(k1) != tuple_map.end());
    assert(tuple_map.find(k1)->second == "Alpha Station");
    assert(tuple_map.find(k2)->second == "Beta Station");

    // Single key tuple (N=1)
    std::unordered_map<SqliteValueTuple<1>, int, SqliteRowHash, SqliteRowEqual> single_map;
    SqliteValueTuple<1> s1(42);
    single_map[s1] = 999;
    assert(single_map[s1] == 999);

    // std::unordered_set integration
    std::unordered_set<SqliteValueTuple<3>, SqliteRowHash, SqliteRowEqual> tuple_set;
    tuple_set.emplace(10, "item_a", 1.5);
    tuple_set.emplace(20, "item_b", 2.5);
    tuple_set.emplace(10, "item_a", 1.5); // Duplicate

    assert(tuple_set.size() == 2);
    SqliteValueTuple<3> probe(10, "item_a", 1.5);
    assert(tuple_set.find(probe) != tuple_set.end());
    SqliteValueTuple<3> missing(30, "item_c", 3.5);
    assert(tuple_set.find(missing) == tuple_set.end());
}

// ============================================================================
// 2. Transparent std::unordered_map with SqliteValueVec
// ============================================================================
void test_transparent_unordered_map_vec() {
    printf("2. Running test_transparent_unordered_map_vec...\n");

    std::unordered_map<SqliteValueVec<4>, std::string, SqliteRowHash, SqliteRowEqual> vec_map;

    SqliteValueVec<4> v1(10, "cluster_1", true);
    SqliteValueVec<4> v2(20, "cluster_2", false);

    vec_map[v1] = "Primary Cluster";
    vec_map[v2] = "Secondary Cluster";

    assert(vec_map.size() == 2);
    assert(vec_map[v1] == "Primary Cluster");
    assert(vec_map[v2] == "Secondary Cluster");

    // Dynamic heap-spilled vector (size > 4)
    SqliteValueVec<4> v_heap(6);
    for (int i = 0; i < 6; ++i) v_heap[i] = SqliteValueOwned(i * 100);
    vec_map[v_heap] = "Spilled Vector";
    assert(vec_map.find(v_heap) != vec_map.end());
    assert(vec_map[v_heap] == "Spilled Vector");
}

// ============================================================================
// 3. Cross-Container Heterogeneous Lookup in B-Tree Maps (std::map)
// ============================================================================
void test_cross_container_transparent_lookup() {
    printf("3. Running test_cross_container_transparent_lookup...\n");

    // Setup map keyed by SqliteValueTuple<3> with transparent SqliteRowLess comparator
    std::map<SqliteValueTuple<3>, std::string, SqliteRowLess> record_map;
    SqliteValueTuple<3> t_key(1001, "US-WEST", 99.5);
    record_map[t_key] = "Server Node #1";

    // 1. Heterogeneous lookup using SqliteValueVec<4>
    SqliteValueVec<4> v_lookup(1001, "US-WEST", 99.5);
    auto it_v = record_map.find(v_lookup);
    assert(it_v != record_map.end());
    assert(it_v->second == "Server Node #1");

    // 2. Heterogeneous lookup using SqliteRowOwnedWrapper from withSqliteRowOwned
    withSqliteRowOwned(3, [&](SqliteRowOwnedWrapper row) {
        row[0] = SqliteValueOwned(1001);
        row[1] = SqliteValueOwned("US-WEST");
        row[2] = SqliteValueOwned(99.5);

        auto it_w = record_map.find(row);
        assert(it_w != record_map.end());
        assert(it_w->second == "Server Node #1");
    });

    // 3. Heterogeneous lookup using SqliteRowView from SQLite query
    sqlite3* db = nullptr;
    assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT 1001, 'US-WEST', 99.5;", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    SqliteRowView rview(stmt);

    auto it_r = record_map.find(rview);
    assert(it_r != record_map.end());
    assert(it_r->second == "Server Node #1");

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

// ============================================================================
// 4. Transparent std::map & std::set (B-Tree) with SqliteRowLess
// ============================================================================
void test_transparent_ordered_map() {
    printf("4. Running test_transparent_ordered_map...\n");

    std::map<SqliteRowOwnedWrapper, std::string, SqliteRowLess> btree_idx;

    SqliteValueOwned v1(10);
    SqliteValueOwned v2(20);
    SqliteValueOwned v3(30);

    SqliteRowOwnedWrapper wrap1(v1);
    SqliteRowOwnedWrapper wrap2(v2);
    SqliteRowOwnedWrapper wrap3(v3);

    btree_idx[wrap1] = "Record 10";
    btree_idx[wrap2] = "Record 20";
    btree_idx[wrap3] = "Record 30";

    // 1. Heterogeneous find using native scalar int
    auto it1 = btree_idx.find(10);
    assert(it1 != btree_idx.end());
    assert(it1->second == "Record 10");

    auto it2 = btree_idx.find(20);
    assert(it2 != btree_idx.end());
    assert(it2->second == "Record 20");

    assert(btree_idx.find(99) == btree_idx.end());

    // 2. Heterogeneous range queries
    auto it_lb = btree_idx.lower_bound(15);
    assert(it_lb != btree_idx.end());
    assert(it_lb->second == "Record 20");

    // 3. Composite Key std::map with Tuple & Vec lookup
    std::map<SqliteValueTuple<2>, int, SqliteRowLess> composite_map;
    composite_map[SqliteValueTuple<2>(1, "alpha")] = 100;
    composite_map[SqliteValueTuple<2>(2, "beta")]  = 200;
    composite_map[SqliteValueTuple<2>(3, "gamma")] = 300;

    SqliteValueVec<2> vec_probe(2, "beta");
    auto it_c = composite_map.find(vec_probe);
    assert(it_c != composite_map.end());
    assert(it_c->second == 200);

    // std::set integration
    std::set<SqliteValueTuple<2>, SqliteRowLess> ordered_set;
    ordered_set.emplace(30, "z");
    ordered_set.emplace(10, "a");
    ordered_set.emplace(20, "m");

    auto set_it = ordered_set.begin();
    assert(set_it->operator[](0).as_int() == 10);
    ++set_it;
    assert(set_it->operator[](0).as_int() == 20);
    ++set_it;
    assert(set_it->operator[](0).as_int() == 30);
}

// ============================================================================
// 5. std::vector of SqliteValueTuple and SqliteValueVec
// ============================================================================
void test_std_vector_integration() {
    printf("5. Running test_std_vector_integration...\n");

    std::vector<SqliteValueTuple<2>> tuple_vec;
    for (int i = 0; i < 10; ++i) {
        SqliteValueTuple<2> t(i, i * 100);
        tuple_vec.push_back(sqlite_move(t));
    }

    assert(tuple_vec.size() == 10);
    for (int i = 0; i < 10; ++i) {
        assert(tuple_vec[i][0].as_int() == i);
        assert(tuple_vec[i][1].as_int() == i * 100);
    }

    std::vector<SqliteValueVec<4>> dyn_vec;
    for (int i = 0; i < 5; ++i) {
        SqliteValueVec<4> v(i + 1);
        for (int j = 0; j <= i; ++j) {
            v[j] = SqliteValueOwned(j * 7);
        }
        dyn_vec.push_back(sqlite_move(v));
    }

    assert(dyn_vec.size() == 5);
    for (int i = 0; i < 5; ++i) {
        assert(dyn_vec[i].size() == i + 1);
        for (int j = 0; j <= i; ++j) {
            assert(dyn_vec[i][j].as_int() == j * 7);
        }
    }
}

// ============================================================================
// 6. Standard Algorithms: std::sort, std::binary_search, std::lower_bound
// ============================================================================
void test_std_algorithms() {
    printf("6. Running test_std_algorithms (std::sort, binary_search, lower_bound)...\n");

    std::vector<SqliteValueTuple<2>> list;
    list.emplace_back(50, "fifty");
    list.emplace_back(10, "ten");
    list.emplace_back(40, "forty");
    list.emplace_back(20, "twenty");
    list.emplace_back(30, "thirty");

    // Sort using SqliteRowLess
    std::sort(list.begin(), list.end(), SqliteRowLess());

    assert(list[0][0].as_int() == 10);
    assert(list[1][0].as_int() == 20);
    assert(list[2][0].as_int() == 30);
    assert(list[3][0].as_int() == 40);
    assert(list[4][0].as_int() == 50);

    // Heterogeneous binary search using a vector probe
    SqliteValueVec<2> target(30, "thirty");
    bool found = std::binary_search(list.begin(), list.end(), target, SqliteRowLess());
    assert(found);

    SqliteValueVec<2> missing(99, "ninety_nine");
    bool not_found = std::binary_search(list.begin(), list.end(), missing, SqliteRowLess());
    assert(!not_found);

    // lower_bound
    auto lb_it = std::lower_bound(list.begin(), list.end(), target, SqliteRowLess());
    assert(lb_it != list.end());
    assert(lb_it->operator[](0).as_int() == 30);
}

// ============================================================================
// 7. std::pair & std::tuple integration
// ============================================================================
void test_std_pair_and_tuple() {
    printf("7. Running test_std_pair_and_tuple...\n");

    typedef std::pair<SqliteValueTuple<2>, SqliteValueVec<2>> ContainerPair;
    ContainerPair p(SqliteValueTuple<2>(10, "key"), SqliteValueVec<2>(20, "val"));

    assert(p.first[0].as_int() == 10);
    assert(p.first[1].as_text() == SqliteStringView("key"));
    assert(p.second[0].as_int() == 20);
    assert(p.second[1].as_text() == SqliteStringView("val"));

    auto t = std::make_tuple(SqliteValueTuple<1>(100), SqliteValueVec<1>("hello"), 3.14159);
    assert(std::get<0>(t)[0].as_int() == 100);
    assert(std::get<1>(t)[0].as_text() == SqliteStringView("hello"));
    assert(std::get<2>(t) > 3.14);
}

int main() {
    printf("=================================================================\n");
    printf("Running Value Containers Standard Library (C++14 STL) Tests\n");
    printf("=================================================================\n");

    test_transparent_unordered_map_tuple();
    test_transparent_unordered_map_vec();
    test_cross_container_transparent_lookup();
    test_transparent_ordered_map();
    test_std_vector_integration();
    test_std_algorithms();
    test_std_pair_and_tuple();

    printf("=================================================================\n");
    printf("All Value Container Standard Library Tests Passed Successfully (100%%)!\n");
    printf("=================================================================\n");
    return 0;
}
