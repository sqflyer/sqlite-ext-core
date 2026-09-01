#define SQLITE_CORE
#include <sqlite3.h>
#include <stdio.h>
#include "sqlite3_sql_runner.hpp"

// Example extension initialization function
static int init_test_extension(sqlite3* db) {
    if (!db) return SQLITE_MISUSE;
    // Extension registration logic goes here
    return SQLITE_OK;
}

// Generates the full standalone main() entry point using the turnkey runner macro
SQLITE_RUN_SQL_EXAMPLE_MAIN("test_script.sql", init_test_extension)
