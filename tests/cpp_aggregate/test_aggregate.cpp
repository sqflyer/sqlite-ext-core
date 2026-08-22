#include <sqlite3.h>
#define SQLITE_CORE
#include "../../include/sqlite3_aggregate.hpp"
#include "../../include/sqlite3_udf.hpp"
#include <stdio.h>
#include <assert.h>
#include <string.h>

// ============================================================================
// 1. Basic Numeric Aggregates
// ============================================================================

struct MyAvg : public SqliteAggregateBase<double> {
    double total = 0.0;
    int count = 0;

    void step(SqliteUdfArgs args) override {
        if (args.size() > 0 && args[0].type() != SQLITE_NULL) {
            total += args[0].as_double();
            count++;
        }
    }

    double finalize() override {
        return count > 0 ? (total / count) : 0.0;
    }
};

struct MySumInt : public SqliteAggregateBase<sqlite3_int64> {
    sqlite3_int64 total = 0;

    void step(SqliteUdfArgs args) override {
        if (args.size() > 0 && args[0].type() != SQLITE_NULL) {
            total += args[0].as_int64();
        }
    }

    sqlite3_int64 finalize() override {
        return total;
    }
};

struct MyCount : public SqliteAggregateBase<int> {
    int count = 0;

    void step(SqliteUdfArgs) override {
        count++;
    }

    int finalize() override {
        return count;
    }
};

void test_basic_numeric_aggregates(sqlite3* db) {
    SqliteUdf::define_aggregate<MyAvg>(db, "my_avg", 1);
    SqliteUdf::define_aggregate<MySumInt>(db, "my_sum_int", 1);
    SqliteUdf::define_aggregate<MyCount>(db, "my_count", 1);

    char* err = nullptr;
    sqlite3_exec(db, "CREATE TABLE num_test(val REAL);", nullptr, nullptr, &err);
    sqlite3_exec(db, "INSERT INTO num_test VALUES (10.0), (20.0), (30.0);", nullptr, nullptr, &err);

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT my_avg(val), my_sum_int(val), my_count(val) FROM num_test;", -1, &stmt, nullptr);
    int rc = sqlite3_step(stmt);
    assert(rc == SQLITE_ROW);

    double avg = sqlite3_column_double(stmt, 0);
    assert(avg >= 19.999 && avg <= 20.001);

    sqlite3_int64 sum = sqlite3_column_int64(stmt, 1);
    assert(sum == 60);

    int count = sqlite3_column_int(stmt, 2);
    assert(count == 3);

    sqlite3_finalize(stmt);
}

// ============================================================================
// 2. String Concatenation Aggregate
// ============================================================================

struct MyConcat : public SqliteAggregateBase<SqliteStringOwned> {
    SqliteStringOwned str;
    bool first = true;

    void step(SqliteUdfArgs args) override {
        if (args.size() > 0 && args[0].type() == SQLITE_TEXT) {
            if (!first) {
                str.append(", ", 2);
            }
            first = false;
            const char* txt = reinterpret_cast<const char*>(sqlite3_value_text(const_cast<sqlite3_value*>(args[0].get())));
            int len = sqlite3_value_bytes(const_cast<sqlite3_value*>(args[0].get()));
            str.append(txt, len);
        }
    }

    SqliteStringOwned finalize() override {
        return sqlite_move_ptr(str);
    }
};

void test_string_concat_aggregate(sqlite3* db) {
    SqliteUdf::define_aggregate<MyConcat>(db, "my_concat", 1);

    char* err = nullptr;
    sqlite3_exec(db, "CREATE TABLE str_test(word TEXT);", nullptr, nullptr, &err);
    sqlite3_exec(db, "INSERT INTO str_test VALUES ('hello'), ('sqlite'), ('world');", nullptr, nullptr, &err);

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT my_concat(word) FROM str_test;", -1, &stmt, nullptr);
    int rc = sqlite3_step(stmt);
    assert(rc == SQLITE_ROW);

    const unsigned char* res = sqlite3_column_text(stmt, 0);
    assert(res != nullptr);
    assert(strcmp(reinterpret_cast<const char*>(res), "hello, sqlite, world") == 0);

    sqlite3_finalize(stmt);
}

// ============================================================================
// 3. Binary Blob Accumulator Aggregate
// ============================================================================

struct MyBlobAccum : public SqliteAggregateBase<SqliteBlobOwned> {
    unsigned char buffer[128];
    int total_bytes = 0;

    void step(SqliteUdfArgs args) override {
        if (args.size() > 0 && args[0].type() == SQLITE_BLOB) {
            const void* data = sqlite3_value_blob(const_cast<sqlite3_value*>(args[0].get()));
            int bytes = sqlite3_value_bytes(const_cast<sqlite3_value*>(args[0].get()));
            if (data && bytes > 0 && total_bytes + bytes <= 128) {
                const unsigned char* src = static_cast<const unsigned char*>(data);
                for (int i = 0; i < bytes; i++) {
                    buffer[total_bytes++] = src[i];
                }
            }
        }
    }

    SqliteBlobOwned finalize() override {
        return SqliteBlobOwned(buffer, total_bytes);
    }
};

void test_blob_aggregate(sqlite3* db) {
    SqliteUdf::define_aggregate<MyBlobAccum>(db, "my_blob_accum", 1);

    char* err = nullptr;
    sqlite3_exec(db, "CREATE TABLE blob_test(data BLOB);", nullptr, nullptr, &err);
    sqlite3_exec(db, "INSERT INTO blob_test VALUES (x'0102'), (x'0304');", nullptr, nullptr, &err);

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT my_blob_accum(data) FROM blob_test;", -1, &stmt, nullptr);
    int rc = sqlite3_step(stmt);
    assert(rc == SQLITE_ROW);

    const void* res = sqlite3_column_blob(stmt, 0);
    int bytes = sqlite3_column_bytes(stmt, 0);
    assert(bytes == 4);

    const unsigned char* ptr = static_cast<const unsigned char*>(res);
    assert(ptr[0] == 0x01 && ptr[1] == 0x02 && ptr[2] == 0x03 && ptr[3] == 0x04);

    sqlite3_finalize(stmt);
}

// ============================================================================
// 4. Multi-Argument & Variadic Aggregates
// ============================================================================

struct WeightedAvg : public SqliteAggregateBase<double> {
    double weighted_sum = 0.0;
    double total_weight = 0.0;

    void step(SqliteUdfArgs args) override {
        if (args.size() >= 2) {
            double val = args[0].as_double();
            double weight = args[1].as_double();
            weighted_sum += (val * weight);
            total_weight += weight;
        }
    }

    double finalize() override {
        return total_weight > 0.0 ? (weighted_sum / total_weight) : 0.0;
    }
};

struct VariadicSum : public SqliteAggregateBase<double> {
    double sum = 0.0;

    void step(SqliteUdfArgs args) override {
        for (int i = 0; i < args.size(); i++) {
            sum += args[i].as_double();
        }
    }

    double finalize() override {
        return sum;
    }
};

void test_multi_arg_and_variadic_aggregates(sqlite3* db) {
    SqliteUdf::define_aggregate<WeightedAvg>(db, "weighted_avg", 2);
    SqliteUdf::define_aggregate<VariadicSum>(db, "variadic_sum", -1);

    char* err = nullptr;
    sqlite3_exec(db, "CREATE TABLE grades(score REAL, weight REAL);", nullptr, nullptr, &err);
    sqlite3_exec(db, "INSERT INTO grades VALUES (80.0, 1.0), (100.0, 3.0);", nullptr, nullptr, &err);

    // Weighted Avg: (80*1 + 100*3) / (1 + 3) = 380 / 4 = 95.0
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT weighted_avg(score, weight) FROM grades;", -1, &stmt, nullptr);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    double res = sqlite3_column_double(stmt, 0);
    assert(res >= 94.999 && res <= 95.001);
    sqlite3_finalize(stmt);

    // Variadic sum over multiple columns
    sqlite3_prepare_v2(db, "SELECT variadic_sum(score, weight, 10.0) FROM grades;", -1, &stmt, nullptr);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    // Row 1: 80 + 1 + 10 = 91, Row 2: 100 + 3 + 10 = 113. Total = 204.0
    double var_res = sqlite3_column_double(stmt, 0);
    assert(var_res >= 203.999 && var_res <= 204.001);
    sqlite3_finalize(stmt);
}

// ============================================================================
// 5. Empty Set Aggregation
// ============================================================================

struct EmptyNullAgg : public SqliteAggregateBase<void> {
    double total = 0.0;
    int count = 0;

    void step(SqliteUdfArgs args) override {
        total += args[0].as_double();
        count++;
    }

    void finalize(sqlite3_context* ctx) override {
        if (count == 0) {
            sqlite3_result_null(ctx);
        } else {
            sqlite3_result_double(ctx, total / count);
        }
    }
};

void test_empty_set_aggregation(sqlite3* db) {
    SqliteUdf::define_aggregate<EmptyNullAgg>(db, "empty_null_avg", 1);

    char* err = nullptr;
    sqlite3_exec(db, "CREATE TABLE empty_tbl(x REAL);", nullptr, nullptr, &err);

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT empty_null_avg(x) FROM empty_tbl;", -1, &stmt, nullptr);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_type(stmt, 0) == SQLITE_NULL);
    sqlite3_finalize(stmt);
}

// ============================================================================
// 6. Group By Aggregations
// ============================================================================

void test_group_by_aggregates(sqlite3* db) {
    char* err = nullptr;
    sqlite3_exec(db, "CREATE TABLE items(category TEXT, price REAL);", nullptr, nullptr, &err);
    sqlite3_exec(db, "INSERT INTO items VALUES ('A', 10.0), ('A', 20.0), ('B', 50.0), ('B', 70.0);", nullptr, nullptr, &err);

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT category, my_avg(price), my_sum_int(price) FROM items GROUP BY category ORDER BY category;", -1, &stmt, nullptr);
    
    // Group A
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "A") == 0);
    double avg_a = sqlite3_column_double(stmt, 1);
    assert(avg_a >= 14.999 && avg_a <= 15.001);
    assert(sqlite3_column_int64(stmt, 2) == 30);

    // Group B
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "B") == 0);
    double avg_b = sqlite3_column_double(stmt, 1);
    assert(avg_b >= 59.999 && avg_b <= 60.001);
    assert(sqlite3_column_int64(stmt, 2) == 120);

    assert(sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
}

// ============================================================================
// 7. Context-Aware Finalize & Custom Error Handling
// ============================================================================

struct ErrorThrowingAgg : public SqliteAggregateBase<void> {
    bool has_error = false;

    void step(sqlite3_context* ctx, SqliteUdfArgs args) override {
        if (args[0].as_double() < 0) {
            has_error = true;
            sqlite3_result_error(ctx, "Negative values not allowed", -1);
        }
    }

    void finalize(sqlite3_context* ctx) override {
        if (!has_error) {
            sqlite3_result_text(ctx, "All positive", -1, SQLITE_STATIC);
        }
    }
};

void test_context_aware_finalize_and_errors(sqlite3* db) {
    SqliteUdf::define_aggregate<ErrorThrowingAgg>(db, "check_pos", 1);

    char* err = nullptr;
    sqlite3_exec(db, "CREATE TABLE err_test(val REAL);", nullptr, nullptr, &err);
    sqlite3_exec(db, "INSERT INTO err_test VALUES (1.0), (-5.0);", nullptr, nullptr, &err);

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT check_pos(val) FROM err_test;", -1, &stmt, nullptr);
    int rc = sqlite3_step(stmt);
    assert(rc == SQLITE_ERROR);
    sqlite3_finalize(stmt);
}

// ============================================================================
// 8. Destructor RAII Resource Cleanup Verification
// ============================================================================

static int g_active_destructors = 0;

struct DestructorTracker : public SqliteAggregateBase<int> {
    DestructorTracker() {
        g_active_destructors++;
    }

    ~DestructorTracker() {
        g_active_destructors--;
    }

    void step(SqliteUdfArgs) override {}
    int finalize() override { return 42; }
};

void test_destructor_cleanup(sqlite3* db) {
    SqliteUdf::define_aggregate<DestructorTracker>(db, "track_dtor", 1);

    int initial_count = g_active_destructors;
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT track_dtor(val) FROM num_test;", -1, &stmt, nullptr);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 42);
    sqlite3_finalize(stmt);

    // Ensure constructor was called and destructor ~T() was cleanly executed during final_proxy
    assert(g_active_destructors == initial_count);
}

// ============================================================================
// 9. Boolean / Logical Aggregates
// ============================================================================

struct LogicalAndAgg : public SqliteAggregateBase<bool> {
    bool all_true = true;
    bool has_rows = false;

    void step(SqliteUdfArgs args) override {
        if (args.size() > 0 && args[0].type() != SQLITE_NULL) {
            has_rows = true;
            if (args[0].as_int64() == 0) {
                all_true = false;
            }
        }
    }

    bool finalize() override {
        return has_rows ? all_true : false;
    }
};

void test_logical_aggregates(sqlite3* db) {
    SqliteUdf::define_aggregate<LogicalAndAgg>(db, "logical_and", 1);

    char* err = nullptr;
    sqlite3_exec(db, "CREATE TABLE bool_test(val INTEGER);", nullptr, nullptr, &err);
    sqlite3_exec(db, "INSERT INTO bool_test VALUES (1), (1), (1);", nullptr, nullptr, &err);

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT logical_and(val) FROM bool_test;", -1, &stmt, nullptr);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 1);
    sqlite3_finalize(stmt);
    
    sqlite3_exec(db, "INSERT INTO bool_test VALUES (0);", nullptr, nullptr, &err);
    sqlite3_prepare_v2(db, "SELECT logical_and(val) FROM bool_test;", -1, &stmt, nullptr);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 0);
    sqlite3_finalize(stmt);
}

// ============================================================================
// 10. Min/Max Aggregates
// ============================================================================

struct MaxIntAgg : public SqliteAggregateBase<sqlite3_int64> {
    sqlite3_int64 current_max = 0;
    bool first = true;

    void step(SqliteUdfArgs args) override {
        if (args.size() > 0 && args[0].type() != SQLITE_NULL) {
            sqlite3_int64 val = args[0].as_int64();
            if (first || val > current_max) {
                current_max = val;
                first = false;
            }
        }
    }

    sqlite3_int64 finalize() override {
        return current_max;
    }
};

void test_min_max_aggregates(sqlite3* db) {
    SqliteUdf::define_aggregate<MaxIntAgg>(db, "max_int", 1);

    char* err = nullptr;
    sqlite3_exec(db, "CREATE TABLE mm_test(val INTEGER);", nullptr, nullptr, &err);
    sqlite3_exec(db, "INSERT INTO mm_test VALUES (10), (50), (20);", nullptr, nullptr, &err);

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT max_int(val) FROM mm_test;", -1, &stmt, nullptr);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int64(stmt, 0) == 50);
    sqlite3_finalize(stmt);
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main() {
    sqlite3_initialize();
    
    sqlite3* db;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
        printf("Failed to open memory database\n");
        return 1;
    }

    printf("1. Testing Basic Numeric Aggregates (Avg, Sum, Count)...\n");
    test_basic_numeric_aggregates(db);

    printf("2. Testing String Concatenation Aggregate (SqliteStringOwned)...\n");
    test_string_concat_aggregate(db);

    printf("3. Testing Binary Blob Accumulator Aggregate (SqliteBlobOwned)...\n");
    test_blob_aggregate(db);

    printf("4. Testing Multi-Argument & Variadic Aggregates...\n");
    test_multi_arg_and_variadic_aggregates(db);

    printf("5. Testing Empty Set Aggregation...\n");
    test_empty_set_aggregation(db);

    printf("6. Testing Group By Multi-Instance Aggregations...\n");
    test_group_by_aggregates(db);

    printf("7. Testing Context-Aware Finalize and Error Handling...\n");
    test_context_aware_finalize_and_errors(db);

    printf("8. Testing Destructor RAII Cleanup...\n");
    test_destructor_cleanup(db);

    printf("9. Testing Boolean / Logical Aggregates...\n");
    test_logical_aggregates(db);

    printf("10. Testing Min/Max Aggregates...\n");
    test_min_max_aggregates(db);

    sqlite3_close(db);
    sqlite3_shutdown();

    printf("\nAll Aggregate Function Framework Tests Passed Successfully!\n");
    return 0;
}
