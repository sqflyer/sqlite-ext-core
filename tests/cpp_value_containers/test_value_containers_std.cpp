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
#include <iterator>
#include "sqlite3_row.hpp"
#include "sqlite3_value_containers.hpp"

namespace std {
    template <typename Iter>
    struct iterator_traits<sqlite_reverse_iterator<Iter>> {
        typedef std::random_access_iterator_tag iterator_category;
        typedef typename sqlite_reverse_iterator<Iter>::value_type value_type;
        typedef typename sqlite_reverse_iterator<Iter>::difference_type difference_type;
        typedef typename sqlite_reverse_iterator<Iter>::pointer pointer;
        typedef typename sqlite_reverse_iterator<Iter>::reference reference;
    };
    template <>
    struct iterator_traits<SqliteRowView::Iterator> {
        typedef std::random_access_iterator_tag iterator_category;
        typedef SqliteRowView::Iterator::value_type value_type;
        typedef SqliteRowView::Iterator::difference_type difference_type;
        typedef SqliteRowView::Iterator::pointer pointer;
        typedef SqliteRowView::Iterator::reference reference;
    };
}

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
    assert(std::get<2>(t) > 3.14);
}

// ============================================================================
// 8. Standard Member Typedefs Compile-Time Compliance Checks
// ============================================================================
template <typename Container, typename ExpectedVal, typename ExpectedRef, typename ExpectedConstRef>
void check_container_typedefs() {
    static_assert(std::is_same<typename Container::value_type, ExpectedVal>::value, "value_type mismatch");
    static_assert(std::is_same<typename Container::size_type, size_t>::value, "size_type must be size_t");
    static_assert(std::is_same<typename Container::difference_type, ptrdiff_t>::value, "difference_type must be ptrdiff_t");
    static_assert(std::is_same<typename Container::reference, ExpectedRef>::value, "reference mismatch");
    static_assert(std::is_same<typename Container::const_reference, ExpectedConstRef>::value, "const_reference mismatch");
    static_assert(sizeof(typename Container::iterator) > 0, "iterator must exist");
    static_assert(sizeof(typename Container::const_iterator) > 0, "const_iterator must exist");
    static_assert(sizeof(typename Container::reverse_iterator) > 0, "reverse_iterator must exist");
    static_assert(sizeof(typename Container::const_reverse_iterator) > 0, "const_reverse_iterator must exist");
}

void test_standard_typedefs_compliance() {
    printf("8. Running test_standard_typedefs_compliance...\n");

    check_container_typedefs<SqliteValueTuple<4>, SqliteValueOwned, SqliteValueOwned&, const SqliteValueOwned&>();
    check_container_typedefs<SqliteValueTuple<0>, SqliteValueOwned, SqliteValueOwned&, const SqliteValueOwned&>();
    check_container_typedefs<SqliteValueTuple<>, SqliteValueOwned, SqliteValueOwned&, const SqliteValueOwned&>();
    check_container_typedefs<SqliteValueVec<4>, SqliteValueOwned, SqliteValueOwned&, const SqliteValueOwned&>();
    check_container_typedefs<SqliteValueVec<0>, SqliteValueOwned, SqliteValueOwned&, const SqliteValueOwned&>();
    check_container_typedefs<SqliteValueVec<>, SqliteValueOwned, SqliteValueOwned&, const SqliteValueOwned&>();
    check_container_typedefs<SqliteRowOwnedWrapper, SqliteValueOwned, SqliteValueOwned&, const SqliteValueOwned&>();
    check_container_typedefs<SqliteRowView, SqliteValueView, SqliteValueView, SqliteValueView>();
}

// ============================================================================
// 9. Standard Algorithms on Forward and Reverse Iterators
// ============================================================================
void test_std_algorithms_deep() {
    printf("9. Running test_std_algorithms_deep (std::copy, transform, count_if, reverse, max_element)...\n");

    // 1. std::transform & std::copy over SqliteValueTuple
    SqliteValueTuple<5> t(10, 20, 30, 40, 50);
    std::vector<int> extracted;
    std::transform(t.begin(), t.end(), std::back_inserter(extracted), [](const SqliteValueOwned& val) {
        return val.as_int() * 2;
    });
    assert(extracted.size() == 5);
    assert(extracted[0] == 20 && extracted[4] == 100);

    // 2. std::reverse_copy using reverse iterators into std::vector
    std::vector<int> rev_extracted;
    std::transform(t.rbegin(), t.rend(), std::back_inserter(rev_extracted), [](const SqliteValueOwned& val) {
        return val.as_int();
    });
    assert(rev_extracted.size() == 5);
    assert(rev_extracted[0] == 50 && rev_extracted[4] == 10);

    // 3. std::count_if on SqliteValueVec
    SqliteValueVec<4> v;
    v.push_back(15);
    v.push_back(25);
    v.push_back(35);
    v.push_back(45);
    v.push_back(55); // Heap spilled!

    auto cnt = std::count_if(v.begin(), v.end(), [](const SqliteValueOwned& val) {
        return val.as_int() > 30;
    });
    assert(cnt == 3);

    // 4. std::max_element & std::min_element
    auto max_it = std::max_element(v.begin(), v.end(), [](const SqliteValueOwned& a, const SqliteValueOwned& b) {
        return a.as_int() < b.as_int();
    });
    assert(max_it != v.end() && max_it->as_int() == 55);

    auto min_it = std::min_element(v.rbegin(), v.rend(), [](const SqliteValueOwned& a, const SqliteValueOwned& b) {
        return a.as_int() < b.as_int();
    });
    assert(min_it != v.rend() && min_it->as_int() == 15);

    // 5. std::is_sorted
    assert(std::is_sorted(v.begin(), v.end(), [](const SqliteValueOwned& a, const SqliteValueOwned& b) {
        return a.as_int() < b.as_int();
    }));
}

// ============================================================================
// 10. Reverse Iterator Arithmetic & Relational Operators
// ============================================================================
void test_reverse_iterator_full_contract() {
    printf("10. Running test_reverse_iterator_full_contract...\n");

    SqliteValueVec<4> vec;
    vec.push_back(100);
    vec.push_back(200);
    vec.push_back(300);
    vec.push_back(400);

    auto rit = vec.rbegin();
    assert(rit->as_int() == 400);
    assert((*rit).as_int() == 400);

    // Pre/Post increment
    auto r1 = rit++;
    assert(r1->as_int() == 400);
    assert(rit->as_int() == 300);

    // Pre/Post decrement
    auto r2 = rit--;
    assert(r2->as_int() == 300);
    assert(rit->as_int() == 400);

    // Arithmetic + / - / += / -=
    auto rit2 = rit + 2;
    assert(rit2->as_int() == 200);
    assert(rit2 - rit == 2);
    assert(rit - rit2 == -2);

    rit += 3;
    assert(rit->as_int() == 100);
    rit -= 3;
    assert(rit == vec.rbegin());

    // Subscript []
    assert(rit[0].as_int() == 400);
    assert(rit[1].as_int() == 300);
    assert(rit[2].as_int() == 200);
    assert(rit[3].as_int() == 100);

    // Relational operators
    auto a = vec.rbegin();
    auto b = vec.rbegin() + 1;
    assert(a < b);
    assert(a <= b);
    assert(b > a);
    assert(b >= a);
    // base() accessor
    assert(a.base() == vec.end());
    assert(vec.rend().base() == vec.begin());
}

// ============================================================================
// 11. Standard Algorithms with Vector & Tuple Modifiers
// ============================================================================
void test_standard_array_vector_modifiers_stl() {
    printf("11. Running test_standard_array_vector_modifiers_stl...\n");

    // std::fill with SqliteValueTuple
    SqliteValueTuple<4> t;
    std::fill(t.begin(), t.end(), SqliteValueOwned(77));
    for (const auto& elem : t) {
        assert(elem.as_int() == 77);
    }

    // std::reverse with SqliteValueVec
    SqliteValueVec<4> v;
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);
    v.push_back(40);
    std::reverse(v.begin(), v.end());
    assert(v[0].as_int() == 40);
    assert(v[1].as_int() == 30);
    assert(v[2].as_int() == 20);
    assert(v[3].as_int() == 10);

    // std::rotate with SqliteValueVec
    std::rotate(v.begin(), v.begin() + 1, v.end());
    assert(v[0].as_int() == 30);
    assert(v[3].as_int() == 40);

    // std::equal & std::lexicographical_compare
    SqliteValueTuple<3> t1(1, 2, 3);
    SqliteValueTuple<3> t2(1, 2, 3);
    SqliteValueTuple<3> t3(1, 2, 4);
    assert(std::equal(t1.begin(), t1.end(), t2.begin()));
    assert(std::lexicographical_compare(t1.begin(), t1.end(), t3.begin(), t3.end()));
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
    test_standard_typedefs_compliance();
    test_std_algorithms_deep();
    test_reverse_iterator_full_contract();
    test_standard_array_vector_modifiers_stl();

    printf("=================================================================\n");
    printf("All Value Container Standard Library Tests Passed Successfully (100%%)!\n");
    printf("=================================================================\n");
    return 0;
}
