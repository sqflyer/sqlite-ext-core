#define SQLITE_CORE
#include "../../include/sqlite3_ext.hpp"
#include "../../include/stl/duo_linear.hpp"
#include "../../include/stl/duo_hash.hpp"
#include "../../include/sqlite3_conn_state.hpp"
#include "../../include/sqlite3_ext_state.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct SessionState {
    int session_id;
    int query_count;
    char user_tag[32];
};

struct SharedDbState {
    int access_counter;
    char db_name[32];
};

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

void test_high_concurrency_conn_state() {
    printf("--- Running test_high_concurrency_conn_state (128 connections) ---\n");
    constexpr int NUM_CONNS = 128;
    sqlite3* conns[NUM_CONNS];

    for (int i = 0; i < NUM_CONNS; ++i) {
        int rc = sqlite3_open(":memory:", &conns[i]);
        assert(rc == SQLITE_OK);

        // Initialize unique state for each connection
        SessionState* state = SqliteConnState<SessionState>::get_or_create(conns[i], [](SessionState* s) {
            s->session_id = 0;
            s->query_count = 0;
            s->user_tag[0] = '\0';
        });
        assert(state != nullptr);
        state->session_id = (i + 1) * 1000;
        state->query_count = (i + 1);
        snprintf(state->user_tag, sizeof(state->user_tag), "conn_%d", i);
    }

    // Verify isolation and O(1) retrieval across all connections
    for (int i = 0; i < NUM_CONNS; ++i) {
        SessionState* state = SqliteConnState<SessionState>::get(conns[i]);
        assert(state != nullptr);
        assert(state->session_id == (i + 1) * 1000);
        assert(state->query_count == (i + 1));
        char expected_tag[32];
        snprintf(expected_tag, sizeof(expected_tag), "conn_%d", i);
        assert(strcmp(state->user_tag, expected_tag) == 0);
    }

    // Close all connections and trigger destructor
    for (int i = 0; i < NUM_CONNS; ++i) {
        SqliteConnState<SessionState>::remove(conns[i]);
        assert(SqliteConnState<SessionState>::get(conns[i]) == nullptr);
        sqlite3_close(conns[i]);
    }
    printf("test_high_concurrency_conn_state: PASSED\n");
}

void test_shared_ext_state_hashmap() {
    printf("--- Running test_shared_ext_state_hashmap ---\n");
    const char *db_file = "shared_test_db.sqlite";
    remove(db_file);

    sqlite3* db1 = nullptr;
    sqlite3* db2 = nullptr;

    assert(sqlite3_open(db_file, &db1) == SQLITE_OK);
    assert(sqlite3_open(db_file, &db2) == SQLITE_OK);

    void* h1 = SqliteExtState<SharedDbState>::init(db1, [](SharedDbState* s) {
        s->access_counter = 50;
        snprintf(s->db_name, sizeof(s->db_name), "shared_test");
    });
    assert(h1 != nullptr);
    assert(sqlite3_create_function_v2(
        db1, "test_fn", 0, SQLITE_UTF8, h1,
        [](sqlite3_context*, int, sqlite3_value**){},
        NULL, NULL, SqliteExtState<SharedDbState>::destructor
    ) == SQLITE_OK);

    void* h2 = SqliteExtState<SharedDbState>::init(db2, nullptr);
    assert(h2 != nullptr);
    assert(sqlite3_create_function_v2(
        db2, "test_fn", 0, SQLITE_UTF8, h2,
        [](sqlite3_context*, int, sqlite3_value**){},
        NULL, NULL, SqliteExtState<SharedDbState>::destructor
    ) == SQLITE_OK);

    SharedDbState* s1 = SqliteExtState<SharedDbState>::get(db1);
    assert(s1 != nullptr);

    SharedDbState* s2 = SqliteExtState<SharedDbState>::get(db2);
    assert(s2 != nullptr);
    // Point to the exact same shared state struct across connections!
    assert(s1 == s2);
    assert(s2->access_counter == 50);

    {
        SqliteExtState<SharedDbState>::WriteGuard lock(s1);
        lock->access_counter += 25;
    }
    assert(s2->access_counter == 75);

    sqlite3_close(db1);
    // db2 still holds state alive
    SharedDbState* s2_again = SqliteExtState<SharedDbState>::get(db2);
    assert(s2_again != nullptr);
    assert(s2_again->access_counter == 75);

    sqlite3_close(db2);
    remove(db_file);
    printf("test_shared_ext_state_hashmap: PASSED\n");
}

void test_cpp_hybrid_state() {
    printf("--- Running test_cpp_hybrid_state ---\n");
    const char *db_file = "cpp_hybrid_test.sqlite";
    remove(db_file);

    sqlite3 *db1 = nullptr;
    sqlite3 *db2 = nullptr;

    assert(sqlite3_open(db_file, &db1) == SQLITE_OK);
    assert(sqlite3_open(db_file, &db2) == SQLITE_OK);

    using AppHybrid = SqliteHybridState<SharedDbState, SessionState>;

    // 1. Initialize hybrid state on db1
    void *holder1 = AppHybrid::init(
        db1,
        [](SharedDbState *ext) {
            ext->access_counter = 1000;
            snprintf(ext->db_name, sizeof(ext->db_name), "hybrid_db");
        },
        [](SessionState *conn) {
            conn->session_id = 1;
            conn->query_count = 0;
            snprintf(conn->user_tag, sizeof(conn->user_tag), "conn1");
        }
    );
    assert(holder1 != nullptr);

    // 2. Initialize hybrid state on db2
    void *holder2 = AppHybrid::init(
        db2,
        nullptr, // Reuses existing shared state
        [](SessionState *conn) {
            conn->session_id = 2;
            conn->query_count = 0;
            snprintf(conn->user_tag, sizeof(conn->user_tag), "conn2");
        }
    );
    assert(holder2 != nullptr);

    // Register UDF on db1
    assert(sqlite3_create_function_v2(
        db1, "cpp_hybrid_query", 0, SQLITE_UTF8,
        holder1,
        [](sqlite3_context *ctx, int, sqlite3_value**) {
            auto state = AppHybrid::from_context(ctx);
            assert(state);
            assert(state.ext != nullptr && state.conn != nullptr);

            // Lock-free connection state update
            state.conn->query_count++;

            // RAII locked update on shared state
            int total = 0;
            {
                AppHybrid::WriteGuard lock(state);
                lock->access_counter += 10;
                total = lock->access_counter;
            }

            char buf[64];
            snprintf(buf, sizeof(buf), "sess=%d,q=%d,tot=%d",
                     state.conn->session_id, state.conn->query_count, total);
            sqlite3_result_text(ctx, buf, -1, SQLITE_TRANSIENT);
        },
        nullptr, nullptr,
        AppHybrid::destructor
    ) == SQLITE_OK);

    // Register UDF on db2
    assert(sqlite3_create_function_v2(
        db2, "cpp_hybrid_query", 0, SQLITE_UTF8,
        holder2,
        [](sqlite3_context *ctx, int, sqlite3_value**) {
            auto state = AppHybrid::from_context(ctx);
            assert(state);

            state.conn->query_count++;
            int total = 0;
            {
                AppHybrid::WriteGuard lock(state);
                lock->access_counter += 10;
                total = lock->access_counter;
            }

            char buf[64];
            snprintf(buf, sizeof(buf), "sess=%d,q=%d,tot=%d",
                     state.conn->session_id, state.conn->query_count, total);
            sqlite3_result_text(ctx, buf, -1, SQLITE_TRANSIENT);
        },
        nullptr, nullptr,
        AppHybrid::destructor
    ) == SQLITE_OK);

    sqlite3_stmt *stmt = nullptr;

    // Query 1 on db1: sess=1, q=1, tot=1010
    assert(sqlite3_prepare_v2(db1, "SELECT cpp_hybrid_query();", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp((const char*)sqlite3_column_text(stmt, 0), "sess=1,q=1,tot=1010") == 0);
    sqlite3_finalize(stmt);

    // Query 2 on db1: sess=1, q=2, tot=1020
    assert(sqlite3_prepare_v2(db1, "SELECT cpp_hybrid_query();", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp((const char*)sqlite3_column_text(stmt, 0), "sess=1,q=2,tot=1020") == 0);
    sqlite3_finalize(stmt);

    // Query 1 on db2: sess=2, q=1, tot=1030 (isolated conn, shared ext!)
    assert(sqlite3_prepare_v2(db2, "SELECT cpp_hybrid_query();", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp((const char*)sqlite3_column_text(stmt, 0), "sess=2,q=1,tot=1030") == 0);
    sqlite3_finalize(stmt);

    // Structured binding resolution via from_db
    auto [ext1, conn1] = AppHybrid::from_db(db1);
    assert(ext1 != nullptr && conn1 != nullptr);
    assert(conn1->session_id == 1 && conn1->query_count == 2);
    assert(ext1->access_counter == 1030);

    auto [ext2, conn2] = AppHybrid::from_db(db2);
    assert(ext2 != nullptr && conn2 != nullptr);
    assert(conn2->session_id == 2 && conn2->query_count == 1);
    assert(ext1 == ext2); // Both point to the exact same shared state struct

    // Close db1 cleanly
    sqlite3_close(db1);

    // db2 still holds shared state alive
    auto [ext2_after, conn2_after] = AppHybrid::from_db(db2);
    assert(ext2_after != nullptr);
    assert(ext2_after->access_counter == 1030);

    sqlite3_close(db2);
    remove(db_file);

    printf("test_cpp_hybrid_state: PASSED\n");
}

int main() {
    printf("==============================================================================\n");
    printf("  RUNNING DUOSTL & STATE REGISTRY TEST SUITE\n");
    printf("==============================================================================\n");

    test_duo_ptr_map();
    test_duo_string_map();
    test_duo_vector();
    test_high_concurrency_conn_state();
    test_shared_ext_state_hashmap();
    test_cpp_hybrid_state();

    printf("\n==============================================================================\n");
    printf("  ALL DUOSTL & STATE REGISTRY TESTS PASSED SUCCESSFULLY!\n");
    printf("==============================================================================\n");
    return 0;
}
