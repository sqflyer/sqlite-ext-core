#define SQLITE_CORE
#include <sqlite3.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "sqlite3_tvf_coro.hpp"
#include "sqlite3_statement.hpp"

// ============================================================================
// TVF 1: Scalar Stackful Generator (generate_series)
// ============================================================================
struct CoroSeriesTvf {
    static constexpr const char* schema() {
        return "CREATE TABLE x(value INT, start HIDDEN, stop HIDDEN, step HIDDEN)";
    }

    static SqliteFiberGenerator<sqlite3_int64> generate(SqliteUdfArgs args) {
        sqlite3_int64 start = (args.size() > 0 && !args[0].is_null()) ? args[0].as_int64() : 0;
        sqlite3_int64 stop  = (args.size() > 1 && !args[1].is_null()) ? args[1].as_int64() : 0;
        sqlite3_int64 step  = (args.size() > 2 && !args[2].is_null()) ? args[2].as_int64() : 1;
        if (step == 0) step = 1;

        return SqliteFiberGenerator<sqlite3_int64>([=](const SqliteFiberGenerator<sqlite3_int64>::YieldHandle& yield) {
            for (sqlite3_int64 v = start; v <= stop; v += step) {
                yield(v);
            }
        });
    }
};

// ============================================================================
// TVF 2: Multi-Column Static Row Generator (SqliteValueTuple<3>)
// ============================================================================
struct CoroMultiColTvf {
    static constexpr const char* schema() {
        return "CREATE TABLE x(id INT, square INT, cube INT, max_n HIDDEN)";
    }

    static SqliteFiberGenerator<SqliteValueTuple<3>> generate(SqliteUdfArgs args) {
        int max_n = (args.size() > 0 && !args[0].is_null()) ? static_cast<int>(args[0].as_int64()) : 3;

        return SqliteFiberGenerator<SqliteValueTuple<3>>([=](const SqliteFiberGenerator<SqliteValueTuple<3>>::YieldHandle& yield) {
            for (int i = 1; i <= max_n; ++i) {
                SqliteValueTuple<3> row;
                row[0] = SqliteValueOwned(static_cast<sqlite3_int64>(i));
                row[1] = SqliteValueOwned(static_cast<sqlite3_int64>(i * i));
                row[2] = SqliteValueOwned(static_cast<sqlite3_int64>(i * i * i));
                yield(row);
            }
        });
    }
};

// ============================================================================
// TVF 3: Dynamic Row String Splitter (SqliteValueVec<2>)
// ============================================================================
struct CoroStringSplitTvf {
    static constexpr const char* schema() {
        return "CREATE TABLE x(idx INT, token TEXT, input_text HIDDEN, delim HIDDEN)";
    }

    static SqliteFiberGenerator<SqliteValueVec<2>> generate(SqliteUdfArgs args) {
        SqliteStringView text = (args.size() > 0 && !args[0].is_null()) ? args[0].as_text() : SqliteStringView("");
        char delim = (args.size() > 1 && !args[1].is_null() && args[1].as_text().length() > 0) ? args[1].as_text().data()[0] : ',';

        return SqliteFiberGenerator<SqliteValueVec<2>>([=](const SqliteFiberGenerator<SqliteValueVec<2>>::YieldHandle& yield) {
            const char* start = text.data();
            const char* p = start;
            int total_len = text.length();
            int idx = 1;

            for (int i = 0; i < total_len; ++i) {
                if (start[i] == delim) {
                    SqliteValueVec<2> row(2);
                    row[0] = SqliteValueOwned(static_cast<sqlite3_int64>(idx++));
                    row[1] = SqliteValueOwned::from_text(p, static_cast<int>((start + i) - p));
                    yield(row);
                    p = start + i + 1;
                }
            }
            if ((start + total_len) >= p) {
                SqliteValueVec<2> row(2);
                row[0] = SqliteValueOwned(static_cast<sqlite3_int64>(idx));
                row[1] = SqliteValueOwned::from_text(p, static_cast<int>((start + total_len) - p));
                yield(row);
            }
        });
    }
};

// ============================================================================
// TEST EXECUTION
// ============================================================================
void run_tvf_coro_tests() {
    printf("=================================================================\n");
    printf("Running Coroutine Table-Valued Function (TVF) Test Suite\n");
    printf("=================================================================\n");

    sqlite3* db = nullptr;
    assert(sqlite3_open(":memory:", &db) == SQLITE_OK);

    // 1. Register TVFs
    printf("1. Registering Coroutine-based TVFs...\n");
    assert(SqliteTvfCoro::define<CoroSeriesTvf>(db, "coro_series") == SQLITE_OK);
    assert(SqliteTvfCoro::define<CoroMultiColTvf>(db, "coro_multi") == SQLITE_OK);
    assert(SqliteTvfCoro::define<CoroStringSplitTvf>(db, "coro_split") == SQLITE_OK);
    printf("   [PASS] TVFs registered successfully.\n");

    // 2. Test Scalar generate_series
    printf("2. Testing SQL query on coro_series(1, 5, 2)...\n");
    {
        SqliteStatement stmt;
        assert(stmt.prepare(db, "SELECT value FROM coro_series(1, 5, 2);") == SQLITE_OK);

        sqlite3_int64 expected[] = { 1, 3, 5 };
        int count = 0;

        while (stmt.step() == SQLITE_ROW) {
            assert(stmt.column_int64(0) == expected[count]);
            count++;
        }
        assert(count == 3);
        printf("   [PASS] Scalar series generated matching rows.\n");
    }

    // 3. Test Multi-Column Static Row TVF
    printf("3. Testing SQL query on coro_multi(4)...\n");
    {
        SqliteStatement stmt;
        assert(stmt.prepare(db, "SELECT id, square, cube FROM coro_multi(4);") == SQLITE_OK);

        int count = 0;
        while (stmt.step() == SQLITE_ROW) {
            count++;
            sqlite3_int64 id = stmt.column_int64(0);
            sqlite3_int64 sq = stmt.column_int64(1);
            sqlite3_int64 cb = stmt.column_int64(2);
            assert(id == count);
            assert(sq == count * count);
            assert(cb == count * count * count);
        }
        assert(count == 4);
        printf("   [PASS] Multi-column rows produced expected values.\n");
    }

    // 4. Test String Splitter TVF
    printf("4. Testing SQL query on coro_split('apple,banana,cherry', ',')...\n");
    {
        SqliteStatement stmt;
        assert(stmt.prepare(db, "SELECT idx, token FROM coro_split('apple,banana,cherry', ',');") == SQLITE_OK);

        const char* expected[] = { "apple", "banana", "cherry" };
        int count = 0;

        while (stmt.step() == SQLITE_ROW) {
            assert(stmt.column_int(0) == count + 1);
            SqliteStringView token = stmt.column_text(1);
            assert(token == expected[count]);
            count++;
        }
        assert(count == 3);
        printf("   [PASS] String splitting TVF yielded matching tokens.\n");
    }

    sqlite3_close(db);
    printf("\nAll Coroutine TVF Tests Passed Cleanly!\n");
}

int main() {
    run_tvf_coro_tests();
    return 0;
}
