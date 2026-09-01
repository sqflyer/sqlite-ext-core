#define SQLITE_CORE
#include <sqlite3.h>
#include <stdio.h>
#include "sqlite3_sql_runner.hpp"

// Generates the full standalone main() entry point using the turnkey file runner macro
SQLITE_RUN_SQL_FILE_MAIN("test_script.sql")
