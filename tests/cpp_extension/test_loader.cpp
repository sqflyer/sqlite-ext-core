#define SQLITE_CORE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <cstdint>
#include <sqlite3.h>
#include "sqlite3_db.hpp"
#include "sqlite3_statement.hpp"

void test_stateless_extension(const char* lib_path) {
    printf("\n=== [TEST 1] Stateless Extension: %s ===\n", lib_path);
    fflush(stdout);

    SqliteDatabaseOwned db(":memory:");
    assert(db.get() != nullptr);
    assert(db.enable_load_extension(true) == SQLITE_OK);

    char* err_msg = nullptr;
    int rc = db.load_extension(lib_path, "sqlite3_stateless_ext_init", &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to load stateless extension: %s\n", err_msg ? err_msg : "unknown error");
        if (err_msg) sqlite3_free(err_msg);
        assert(false);
    }

    // 1. Scalar UDFs
    {
        SqliteStatement stmt = db.prepare("SELECT stateless_add(100, 250);");
        assert(stmt.next());
        assert(stmt.column_int64(0) == 350);
        printf("  [PASS] stateless_add(100, 250) = 350\n");
    }
    {
        SqliteStatement stmt = db.prepare("SELECT stateless_greet('Developer');");
        assert(stmt.next());
        SqliteStringView text = stmt.column_text(0);
        assert(memcmp(text.data(), "Greetings, Developer!", text.length()) == 0);
        printf("  [PASS] stateless_greet('Developer') = 'Greetings, Developer!'\n");
    }

    // 2. Aggregate Function
    {
        assert(db.exec("CREATE TABLE nums(x INT);") == SQLITE_OK);
        assert(db.exec("INSERT INTO nums VALUES (1), (2), (3), (4);") == SQLITE_OK);
        SqliteStatement stmt = db.prepare("SELECT stateless_sum_sq(x) FROM nums;");
        assert(stmt.next());
        assert(stmt.column_int64(0) == (1 + 4 + 9 + 16)); // 30
        printf("  [PASS] stateless_sum_sq(x) = 30\n");
    }

    // 3. Table-Valued Function (TVF)
    {
        SqliteStatement stmt = db.prepare("SELECT value FROM stateless_range(10, 14);");
        int count = 0;
        sqlite3_int64 sum = 0;
        while (stmt.next()) {
            count++;
            sum += stmt.column_int64(0);
        }
        assert(count == 5);
        assert(sum == (10 + 11 + 12 + 13 + 14)); // 60
        printf("  [PASS] stateless_range(10, 14) yielded 5 rows with sum 60\n");
    }

    // 4. Virtual Table
    {
        assert(db.exec("CREATE VIRTUAL TABLE echo_tbl USING stateless_echo();") == SQLITE_OK);
        SqliteStatement stmt = db.prepare("SELECT id, score FROM echo_tbl;");
        int rows = 0;
        while (stmt.next()) {
            rows++;
            int id = stmt.column_int(0);
            int score = stmt.column_int(1);
            assert(score == id * 10);
        }
        assert(rows == 3);
        printf("  [PASS] stateless_echo virtual table yielded 3 rows with score = id * 10\n");
    }

    printf(">>> [TEST 1 PASSED] Stateless extension fully verified.\n");
    fflush(stdout);
}

void test_stateful_extension(const char* lib_path) {
    printf("\n=== [TEST 2] Stateful Extension: %s ===\n", lib_path);
    fflush(stdout);

    SqliteDatabaseOwned db1(":memory:");
    assert(db1.enable_load_extension(true) == SQLITE_OK);

    char* err_msg = nullptr;
    int rc = db1.load_extension(lib_path, "sqlite3_stateful_ext_init", &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to load stateful extension: %s\n", err_msg ? err_msg : "unknown error");
        if (err_msg) sqlite3_free(err_msg);
        assert(false);
    }

    // 1. Initial State & Counter Increments
    {
        SqliteStatement stmt_get = db1.prepare("SELECT stateful_get();");
        assert(stmt_get.next());
        assert(stmt_get.column_int(0) == 500);

        SqliteStatement stmt_inc = db1.prepare("SELECT stateful_inc();");
        assert(stmt_inc.next());
        assert(stmt_inc.column_int(0) == 501);

        stmt_inc.reset();
        assert(stmt_inc.next());
        assert(stmt_inc.column_int(0) == 502);

        stmt_get.reset();
        assert(stmt_get.next());
        assert(stmt_get.column_int(0) == 502);
        printf("  [PASS] stateful_inc / stateful_get counter (500 -> 501 -> 502)\n");
    }

    // 2. Tagged Aggregate with shared session tag
    {
        assert(db1.exec("CREATE TABLE items(name TEXT);") == SQLITE_OK);
        assert(db1.exec("INSERT INTO items VALUES ('apple'), ('banana'), ('cherry');") == SQLITE_OK);

        SqliteStatement stmt_concat = db1.prepare("SELECT stateful_concat(name) FROM items;");
        assert(stmt_concat.next());
        SqliteStringView res = stmt_concat.column_text(0);
        assert(memcmp(res.data(), "SESSION_TEST:apple,banana,cherry", res.length()) == 0);
        printf("  [PASS] stateful_concat = 'SESSION_TEST:apple,banana,cherry'\n");

        // Mutate tag via UDF and re-evaluate
        assert(db1.exec("SELECT stateful_set_tag('NEW_TAG');") == SQLITE_OK);
        stmt_concat.reset();
        assert(stmt_concat.next());
        SqliteStringView res2 = stmt_concat.column_text(0);
        assert(memcmp(res2.data(), "NEW_TAG:apple,banana,cherry", res2.length()) == 0);
        printf("  [PASS] stateful_set_tag('NEW_TAG') updated stateful_concat output\n");
    }

    // 3. Stateful TVF
    {
        SqliteStatement stmt = db1.prepare("SELECT metric_name, metric_value FROM stateful_metrics();");
        assert(stmt.next());
        assert(strcmp(stmt.column_text(0), "counter") == 0);
        assert(stmt.column_int(1) == 502); // counter was mutated to 502!

        assert(stmt.next());
        assert(strcmp(stmt.column_text(0), "cache_0") == 0);
        assert(stmt.column_int(1) == 111);

        assert(stmt.next());
        assert(strcmp(stmt.column_text(0), "cache_1") == 0);
        assert(stmt.column_int(1) == 222);
        printf("  [PASS] stateful_metrics TVF accurately streamed live shared state values\n");
    }

    // 4. Stateful Virtual Table
    {
        assert(db1.exec("CREATE VIRTUAL TABLE my_cache USING stateful_cache();") == SQLITE_OK);
        SqliteStatement stmt = db1.prepare("SELECT slot, val FROM my_cache;");
        int rows = 0;
        while (stmt.next()) {
            int slot = stmt.column_int(0);
            int val = stmt.column_int(1);
            assert(val == (slot + 1) * 111);
            rows++;
        }
        assert(rows == 5);
        printf("  [PASS] stateful_cache virtual table read state slots 0..4 correctly\n");
    }

    // 5. Connection Isolation Verification
    {
        SqliteDatabaseOwned db2(":memory:");
        assert(db2.enable_load_extension(true) == SQLITE_OK);
        assert(db2.load_extension(lib_path, "sqlite3_stateful_ext_init", nullptr) == SQLITE_OK);

        // db2 starts fresh at 500, completely independent of db1's 502
        SqliteStatement stmt2 = db2.prepare("SELECT stateful_get();");
        assert(stmt2.next());
        assert(stmt2.column_int(0) == 500);
        printf("  [PASS] Per-connection isolation verified (db2 starts fresh at 500)\n");
    }

    printf(">>> [TEST 2 PASSED] Stateful extension fully verified.\n");
    fflush(stdout);
}

void test_mixed_extension(const char* lib_path) {
    printf("\n=== [TEST 3] Mixed Extension (Default Entrypoint): %s ===\n", lib_path);
    fflush(stdout);

    SqliteDatabaseOwned db(":memory:");
    assert(db.enable_load_extension(true) == SQLITE_OK);

    // Load using default entrypoint (nullptr proc name resolves sqlite3_extension_init)
    char* err_msg = nullptr;
    int rc = db.load_extension(lib_path, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to load mixed extension via default entrypoint: %s\n", err_msg ? err_msg : "unknown error");
        if (err_msg) sqlite3_free(err_msg);
        assert(false);
    }
    printf("  [PASS] Extension loaded via default entrypoint (sqlite3_extension_init)\n");

    // 1. Stateless Component: mixed_multiply(2.5, 4.0)
    {
        SqliteStatement stmt = db.prepare("SELECT mixed_multiply(2.5, 4.0);");
        assert(stmt.next());
        assert(stmt.column_double(0) == 10.0);
        printf("  [PASS] Stateless mixed_multiply(2.5, 4.0) = 10.0\n");
    }

    // 2. Stateless TVF: mixed_iota(4)
    {
        SqliteStatement stmt = db.prepare("SELECT val FROM mixed_iota(4);");
        int count = 0;
        int sum = 0;
        while (stmt.next()) {
            count++;
            sum += stmt.column_int(0);
        }
        assert(count == 4);
        assert(sum == (1 + 2 + 3 + 4)); // 10
        printf("  [PASS] Stateless mixed_iota(4) yielded 4 rows with sum 10\n");
    }

    // 3. Stateful Scalar Audit Log
    {
        SqliteStatement stmt = db.prepare("SELECT mixed_audit();");
        assert(stmt.next());
        assert(stmt.column_int(0) == 1);

        stmt.reset();
        assert(stmt.next());
        assert(stmt.column_int(0) == 2);
        printf("  [PASS] Stateful mixed_audit() tracked calls (1 -> 2)\n");
    }

    // 4. Stateful Aggregate: mixed_weighted_avg
    {
        assert(db.exec("CREATE TABLE scores(val REAL, w REAL);") == SQLITE_OK);
        assert(db.exec("INSERT INTO scores VALUES (80.0, 1.0), (90.0, 3.0);") == SQLITE_OK);

        // Expected: (80*1 + 90*3) / (1 + 3) = 350 / 4 = 87.5
        SqliteStatement stmt = db.prepare("SELECT mixed_weighted_avg(val, w) FROM scores;");
        assert(stmt.next());
        assert(stmt.column_double(0) == 87.5);
        printf("  [PASS] Stateful mixed_weighted_avg(val, w) = 87.5\n");
    }

    printf(">>> [TEST 3 PASSED] Mixed extension fully verified.\n");
    fflush(stdout);
}

int main(int argc, char** argv) {
    const char* stateless_lib = (argc > 1) ? argv[1] : "./bin/libtest_ext_stateless.dll";
    const char* stateful_lib  = (argc > 2) ? argv[2] : "./bin/libtest_ext_stateful.dll";
    const char* mixed_lib     = (argc > 3) ? argv[3] : "./bin/libtest_ext_mixed.dll";

    printf("=================================================================\n");
    printf("Starting C++ Extension Creator Verification Test Suite\n");
    printf("=================================================================\n");
    fflush(stdout);

    test_stateless_extension(stateless_lib);
    test_stateful_extension(stateful_lib);
    test_mixed_extension(mixed_lib);

    printf("\n=================================================================\n");
    printf("ALL 3 EXTENSION CREATOR TEST SUITES PASSED SUCCESSFULLY (100%%)!\n");
    printf("=================================================================\n");
    fflush(stdout);
    return 0;
}
