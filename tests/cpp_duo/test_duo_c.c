#define SQLITE_CORE
#include <sqlite3.h>
#include "../../include/stl/duo_hash.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static void test_duo_c_hashmap() {
    printf("--- Running test_duo_c_hashmap ---\n");
    // Create map with int key (size 4) and int value (size 4)
    duo_hashmap_t *map = duo_hashmap_new(sizeof(int), sizeof(int), 16, 0, 0, NULL, NULL, NULL, NULL, NULL);
    assert(map != NULL);
    assert(duo_hashmap_count(map) == 0);

    // Insert 10 key-values
    for (int i = 1; i <= 10; i++) {
        int key = i * 100;
        int val = i * 1000;
        assert(duo_hashmap_set(map, &key, &val));
    }
    assert(duo_hashmap_count(map) == 10);

    // Verify lookup
    for (int i = 1; i <= 10; i++) {
        int key = i * 100;
        int *val = (int*)duo_hashmap_get(map, &key);
        assert(val != NULL);
        assert(*val == i * 1000);
        assert(duo_hashmap_contains(map, &key));
    }

    // Verify missing key
    int missing_key = 99999;
    assert(duo_hashmap_get(map, &missing_key) == NULL);
    assert(!duo_hashmap_contains(map, &missing_key));

    // Delete key
    int del_key = 300;
    assert(duo_hashmap_delete(map, &del_key));
    assert(!duo_hashmap_contains(map, &del_key));
    assert(duo_hashmap_count(map) == 9);
    assert(!duo_hashmap_delete(map, &del_key)); // already deleted

    // Free map
    duo_hashmap_free(map);
    printf("test_duo_c_hashmap: PASSED\n");
}

int main() {
    printf("==============================================================================\n");
    printf("  RUNNING DUOSTL PURE C CONTAINER TEST SUITE\n");
    printf("==============================================================================\n");

    test_duo_c_hashmap();

    printf("\n==============================================================================\n");
    printf("  ALL DUOSTL PURE C TESTS PASSED SUCCESSFULLY!\n");
    printf("==============================================================================\n");
    return 0;
}
