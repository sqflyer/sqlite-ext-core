#ifndef TEST_TU_SHARED_HPP
#define TEST_TU_SHARED_HPP

#include <sqlite3.h>
#define SQLITE_CORE
#include "sqlite3_ext.hpp"

struct TuAppState {
    int counter;
    char tag[64];

    TuAppState() : counter(100) {
        tag[0] = '\0';
    }
};

// Declarations of registration functions exported from different translation units
void register_tu_a_functions(SqliteDatabaseView db);
void register_tu_b_functions(SqliteDatabaseView db);

#endif // TEST_TU_SHARED_HPP
