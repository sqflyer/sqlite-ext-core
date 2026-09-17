#define SQLITE_CORE
#include <sqlite3.h>
#include "../../include/stl/duo_linear.hpp"
#include "../../include/stl/duo_hash.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void test_duo_ptr_map() {
    printf("--- Running test_duo_ptr_map ---\n");
    duo::HashMap<uintptr_t, int> map;
    assert(map.empty());
    assert(map.size() == 0);

    // Test insert / set
    map.insert(0x1000, 42);
    map.insert(0x2000, 84);
    map.insert(0x3000, 126);

    assert(!map.empty());
    assert(map.size() == 3);
    assert(map.contains(0x1000));
    assert(map.contains(0x2000));
    assert(map.contains(0x3000));
    assert(!map.contains(0x4000));

    // Test get
    int* v1 = map.get(0x1000);
    assert(v1 != nullptr && *v1 == 42);
    int* v2 = map.get(0x2000);
    assert(v2 != nullptr && *v2 == 84);
    int* v3 = map.get(0x3000);
    assert(v3 != nullptr && *v3 == 126);
    assert(map.get(0x9999) == nullptr);

    // Test erase
    assert(map.erase(0x2000));
    assert(!map.contains(0x2000));
    assert(map.size() == 2);
    assert(!map.erase(0x2000));

    // Test iteration via range-for
    int sum = 0;
    for (auto it = map.begin(); it != map.end(); ++it) {
        sum += it.value();
    }
    assert(sum == (42 + 126));

    // Test Move semantics
    duo::HashMap<uintptr_t, int> moved_map = duo::move(map);
    assert(map.empty());
    assert(moved_map.size() == 2);
    assert(moved_map.contains(0x1000));

    moved_map.clear();
    assert(moved_map.empty());
    printf("test_duo_ptr_map: PASSED\n");
}

void test_duo_string_map() {
    printf("--- Running test_duo_string_map ---\n");
    duo::HashMap<duo::String, int> map;
    assert(map.empty());
    assert(map.size() == 0);

    // Test insert
    map.insert(duo::String("alpha"), 100);
    map.insert(duo::String("beta"), 200);
    map.insert(duo::String("gamma"), 300);

    assert(!map.empty());
    assert(map.size() == 3);
    assert(map.contains(duo::String("alpha")));
    assert(map.contains(duo::String("beta")));
    assert(map.contains(duo::String("gamma")));
    assert(!map.contains(duo::String("delta")));

    // Test get
    int* v_alpha = map.get(duo::String("alpha"));
    assert(v_alpha != nullptr && *v_alpha == 100);
    int* v_beta = map.get(duo::String("beta"));
    assert(v_beta != nullptr && *v_beta == 200);
    int* v_gamma = map.get(duo::String("gamma"));
    assert(v_gamma != nullptr && *v_gamma == 300);

    // Test erase
    assert(map.erase(duo::String("beta")));
    assert(!map.contains(duo::String("beta")));
    assert(map.size() == 2);

    // Test iteration
    int sum = 0;
    for (auto it = map.begin(); it != map.end(); ++it) {
        sum += it.value();
    }
    assert(sum == (100 + 300));

    // Test Move semantics
    duo::HashMap<duo::String, int> moved_map = duo::move(map);
    assert(map.empty());
    assert(moved_map.size() == 2);
    assert(moved_map.contains(duo::String("alpha")));

    moved_map.clear();
    assert(moved_map.empty());
    printf("test_duo_string_map: PASSED\n");
}

void test_duo_vector() {
    printf("--- Running test_duo_vector ---\n");
    duo::Vector<int> vec;
    assert(vec.empty());
    assert(vec.size() == 0);

    for (int i = 1; i <= 10; ++i) {
        vec.push_back(i * 10);
    }
    assert(vec.size() == 10);
    assert(!vec.empty());

    for (size_t i = 0; i < vec.size(); ++i) {
        assert(vec[i] == (int)(i + 1) * 10);
    }

    assert(vec.back() == 100);
    vec.pop_back();
    assert(vec.size() == 9);

    // Test Move semantics
    duo::Vector<int> moved_vec = duo::move(vec);
    assert(vec.empty());
    assert(moved_vec.size() == 9);
    assert(moved_vec[0] == 10);

    moved_vec.clear();
    assert(moved_vec.empty());
    printf("test_duo_vector: PASSED\n");
}

void test_duo_string() {
    printf("--- Running test_duo_string ---\n");
    duo::String s("Hello");
    assert(s.size() == 5);
    assert(strcmp(s.c_str(), "Hello") == 0);

    s.append(", DuoSTL!");
    assert(s.size() == 14);
    assert(strcmp(s.c_str(), "Hello, DuoSTL!") == 0);

    duo::String s2 = duo::move(s);
    assert(s.empty());
    assert(s2.size() == 14);
    assert(strcmp(s2.c_str(), "Hello, DuoSTL!") == 0);

    printf("test_duo_string: PASSED\n");
}

int main() {
    printf("==============================================================================\n");
    printf("  RUNNING DUOSTL C++ CONTAINER TEST SUITE\n");
    printf("==============================================================================\n");

    test_duo_ptr_map();
    test_duo_string_map();
    test_duo_vector();
    test_duo_string();

    printf("\n==============================================================================\n");
    printf("  ALL DUOSTL TESTS PASSED SUCCESSFULLY!\n");
    printf("==============================================================================\n");
    return 0;
}
