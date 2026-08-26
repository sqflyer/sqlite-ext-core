#include "test_tu_shared.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>

SQLITE_EXTENSION_INIT1

int main() {
    printf("=================================================================\n");
    printf("Running Multi-Translation Unit (Multi-TU) & ODR Test Suite\n");
    printf("=================================================================\n");

    sqlite3* raw_db = nullptr;
    assert(sqlite3_open(":memory:", &raw_db) == SQLITE_OK);
    SqliteDatabaseView db(raw_db);

    printf("1. Initializing shared state in main...\n");
    void* state_token = SqliteExt::init_state<TuAppState>(db);
    assert(state_token != nullptr);

    printf("2. Registering functions from Translation Unit A (tu_inc, tu_set_tag)...\n");
    register_tu_a_functions(db);

    printf("3. Registering functions from Translation Unit B (tu_get_stats)...\n");
    register_tu_b_functions(db);

    printf("4. Executing queries mutating state in TU-A and reading in TU-B...\n");
    sqlite3_stmt* stmt = nullptr;
    
    // Initial stats check (should be default: counter=100, tag="")
    assert(sqlite3_prepare_v2(raw_db, "SELECT tu_get_stats();", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    const char* stats1 = (const char*)sqlite3_column_text(stmt, 0);
    assert(strcmp(stats1, "counter=100,tag=") == 0);
    sqlite3_finalize(stmt);

    // Call TU-A functions: increment counter twice (100 -> 110 -> 120) and set tag to "MULTI_TU_TEST"
    assert(sqlite3_prepare_v2(raw_db, "SELECT tu_inc(), tu_inc(), tu_set_tag('MULTI_TU_TEST');", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 110);
    assert(sqlite3_column_int(stmt, 1) == 120);
    sqlite3_finalize(stmt);

    // Read mutated state from TU-B function (must see counter=120, tag="MULTI_TU_TEST")
    assert(sqlite3_prepare_v2(raw_db, "SELECT tu_get_stats();", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    const char* stats2 = (const char*)sqlite3_column_text(stmt, 0);
    assert(strcmp(stats2, "counter=120,tag=MULTI_TU_TEST") == 0);
    sqlite3_finalize(stmt);

    printf("   [PASS] Cross-TU state sharing confirmed!\n");

    sqlite3_close(raw_db);
    printf("\nAll Multi-TU & ODR Linkage Tests Passed Successfully!\n");
    return 0;
}
