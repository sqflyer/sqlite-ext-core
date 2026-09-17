#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <extension_path>\n", argv[0]);
        return 1;
    }

    const char *ext_path = argv[1];
    printf("Starting C hybrid state leak check with: %s\n", ext_path);

    int num_dbs = 3;
    int conns_per_db = 10;
    sqlite3 *handles[3][10];

    for (int d = 0; d < num_dbs; d++) {
        char db_path[64];
        snprintf(db_path, sizeof(db_path), "test_leak_hybrid_%d.sqlite", d);
        remove(db_path);

        for (int c = 0; c < conns_per_db; c++) {
            assert(sqlite3_open(db_path, &handles[d][c]) == SQLITE_OK);
            assert(sqlite3_enable_load_extension(handles[d][c], 1) == SQLITE_OK);
            
            char *err = NULL;
            int rc = sqlite3_load_extension(handles[d][c], ext_path, "sqlite3_myext_init", &err);
            if (rc != SQLITE_OK) {
                fprintf(stderr, "Failed to load extension: %s\n", err);
                sqlite3_free(err);
                return 1;
            }

            sqlite3_stmt *stmt = NULL;
            for (int k = 0; k < 20; k++) {
                assert(sqlite3_prepare_v2(handles[d][c], "SELECT test_conn_counter();", -1, &stmt, NULL) == SQLITE_OK);
                assert(sqlite3_step(stmt) == SQLITE_ROW);
                sqlite3_finalize(stmt);

                assert(sqlite3_prepare_v2(handles[d][c], "SELECT test_ext_counter();", -1, &stmt, NULL) == SQLITE_OK);
                assert(sqlite3_step(stmt) == SQLITE_ROW);
                sqlite3_finalize(stmt);

                assert(sqlite3_prepare_v2(handles[d][c], "SELECT test_hybrid_from_db();", -1, &stmt, NULL) == SQLITE_OK);
                assert(sqlite3_step(stmt) == SQLITE_ROW);
                sqlite3_finalize(stmt);
            }
        }
    }

    // Close all connections cleanly
    for (int d = 0; d < num_dbs; d++) {
        for (int c = 0; c < conns_per_db; c++) {
            assert(sqlite3_close(handles[d][c]) == SQLITE_OK);
        }
        char db_path[64];
        snprintf(db_path, sizeof(db_path), "test_leak_hybrid_%d.sqlite", d);
        remove(db_path);
    }

    printf("C hybrid state leak check complete.\n");
    return 0;
}
