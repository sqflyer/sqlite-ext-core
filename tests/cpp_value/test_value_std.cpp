#define SQLITE_CORE
#include <sqlite3.h>
#include "../../include/sqlite3_value.hpp"
#include <cstdio>
#include <cassert>
#include <cstring>
#include <map>

// ============================================================================
// Transparent Hashing & std::map Heterogeneous Lookup Tests
// ============================================================================
void test_transparent_hashing(sqlite3* db) {
    printf("Testing Transparent Hashing (SqliteValueHash & SqliteValueEqual)...\n");

    SqliteValueHash hasher;
    SqliteValueEqual eq;

    // 1. Prepare SQLite values from a statement
    sqlite3_stmt* stmt = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT 42, 'alice', 'bob_very_long_name_exceeding_13_chars', 3.14;", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);

    SqliteValueView v_int = SqliteValueView::from_column(stmt, 0);
    SqliteValueView v_alice = SqliteValueView::from_column(stmt, 1);
    SqliteValueView v_bob = SqliteValueView::from_column(stmt, 2);
    SqliteValueView v_pi = SqliteValueView::from_column(stmt, 3);

    SqliteValueOwned o_int(42LL);
    SqliteValueOwned o_alice = SqliteValueOwned::from_text("alice");
    SqliteValueOwned o_bob = v_bob.to_owned();
    SqliteValueOwned o_pi(3.14);

    SqliteStringView str_alice("alice", 5);

    // 2. Verify exact mathematical hash equivalence across all representations
    assert(hasher(o_int) == hasher(v_int));
    assert(hasher(o_int) == hasher(42LL));
    assert(hasher(o_int) == hasher(42));

    assert(hasher(o_alice) == hasher(v_alice));
    assert(hasher(o_alice) == hasher(str_alice));
    assert(hasher(o_alice) == hasher("alice"));

    assert(hasher(o_bob) == hasher(v_bob));
    assert(hasher(o_pi) == hasher(v_pi));
    assert(hasher(o_pi) == hasher(3.14));

    // 3. Verify transparent equality
    assert(eq(o_int, v_int));
    assert(eq(o_int, 42LL));
    assert(eq(v_int, 42LL));

    assert(eq(o_alice, v_alice));
    assert(eq(o_alice, str_alice));
    assert(eq(o_alice, "alice"));
    assert(eq(v_alice, str_alice));
    assert(eq(v_alice, "alice"));

    // 4. Heterogeneous Map Lookup with std::less<>
    std::map<SqliteValueOwned, int, std::less<>> map;
    map[SqliteValueOwned(42LL)] = 100;
    map[SqliteValueOwned::from_text("alice")] = 200;
    map[v_bob.to_owned()] = 300;
    map[SqliteValueOwned(3.14)] = 400;

    assert(map.find(v_int) != map.end());
    assert(map.find(v_int)->second == 100);

    assert(map.find(v_alice) != map.end());
    assert(map.find(v_alice)->second == 200);

    assert(map.find(str_alice) != map.end());
    assert(map.find(str_alice)->second == 200);

    assert(map.find("alice") != map.end());
    assert(map.find("alice")->second == 200);

    assert(map.find(42LL) != map.end());
    assert(map.find(42LL)->second == 100);

    sqlite3_finalize(stmt);
}

int main() {
    printf("================================================================\n");
    printf("RUNNING SQLITE VALUE STD TESTS (Transparent Hashing & STL Map)\n");
    printf("================================================================\n");

    sqlite3_initialize();
    sqlite3* db = nullptr;
    assert(sqlite3_open(":memory:", &db) == SQLITE_OK);

    test_transparent_hashing(db);

    sqlite3_close(db);
    sqlite3_shutdown();

    printf("================================================================\n");
    printf("ALL SQLITE VALUE STD TESTS PASSED SUCCESSFULLY!\n");
    printf("================================================================\n");
    return 0;
}
