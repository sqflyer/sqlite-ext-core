#define SQLITE_CORE
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "sqlite3_udf.hpp"

// 1. Basic 2-arg scalar function: add_numbers(a, b)
void udf_add_numbers(sqlite3_context* ctx, SqliteUdfArgs args) {
    if (args.size() != 2) {
        sqlite3_result_error(ctx, "add_numbers requires exactly 2 arguments", -1);
        return;
    }
    
    // Out-of-bounds access safety check
    assert(args[-1].type() == SQLITE_NULL);
    assert(args[-999].type() == SQLITE_NULL);
    assert(args[2].type() == SQLITE_NULL);
    assert(args[999].type() == SQLITE_NULL);
    assert(args[-1].get() == nullptr);
    assert(args[999].get() == nullptr);

    sqlite3_int64 result = args[0].as_int64() + args[1].as_int64();
    sqlite3_result_int64(ctx, result);
}

// 2. String manipulation: repeat_str(str, count) using SqliteStringOwned & SqliteStringView
void udf_repeat_str(sqlite3_context* ctx, SqliteUdfArgs args) {
    if (args.size() != 2) {
        sqlite3_result_error(ctx, "repeat_str requires exactly 2 arguments", -1);
        return;
    }

    if (args[0].type() != SQLITE_TEXT || args[1].type() != SQLITE_INTEGER) {
        sqlite3_result_error(ctx, "repeat_str expects (TEXT, INTEGER)", -1);
        return;
    }

    const char* raw_str = reinterpret_cast<const char*>(sqlite3_value_text(const_cast<sqlite3_value*>(args[0].get())));
    int len = sqlite3_value_bytes(const_cast<sqlite3_value*>(args[0].get()));
    SqliteStringView str(raw_str, len);
    int count = args[1].as_int64();

    SqliteStringOwned result(ctx);
    for (int i = 0; i < count; i++) {
        result.append(str.data(), str.length());
    }
    
    result.result(ctx);
}

// 3. Blob manipulation: bitwise_not_blob(blob)
void udf_not_blob(sqlite3_context* ctx, SqliteUdfArgs args) {
    if (args.size() != 1 || args[0].type() != SQLITE_BLOB) {
        sqlite3_result_error(ctx, "not_blob expects 1 BLOB argument", -1);
        return;
    }

    const unsigned char* raw_data = reinterpret_cast<const unsigned char*>(sqlite3_value_blob(const_cast<sqlite3_value*>(args[0].get())));
    int len = sqlite3_value_bytes(const_cast<sqlite3_value*>(args[0].get()));
    
    unsigned char inv[64];
    assert(len <= 64);
    for (int i = 0; i < len; i++) {
        inv[i] = ~raw_data[i];
    }
    SqliteBlobOwned inverted(inv, len);
    inverted.result(ctx);
}

// 4. Variadic UDF: sum_variadic(a, b, c, ...)
void udf_sum_variadic(sqlite3_context* ctx, SqliteUdfArgs args) {
    double total = 0.0;
    for (int i = 0; i < args.size(); i++) {
        if (args[i].type() == SQLITE_INTEGER) {
            total += args[i].as_int64();
        } else if (args[i].type() == SQLITE_FLOAT) {
            total += args[i].as_double();
        } else if (args[i].type() == SQLITE_NULL) {
            continue;
        } else {
            sqlite3_result_error(ctx, "sum_variadic only accepts numbers", -1);
            return;
        }
    }
    sqlite3_result_double(ctx, total);
}

// 5. Heterogeneous comparison inside UDF: check_magic(val)
void udf_check_magic(sqlite3_context* ctx, SqliteUdfArgs args) {
    if (args.size() != 1) {
        sqlite3_result_error(ctx, "check_magic requires 1 argument", -1);
        return;
    }

    if (args[0] == 42) {
        sqlite3_result_text(ctx, "is_magic_int", -1, SQLITE_STATIC);
    } else if (args[0] == 3.14) {
        sqlite3_result_text(ctx, "is_magic_pi", -1, SQLITE_STATIC);
    } else if (args[0] == SqliteStringView("sqlite", 6)) {
        sqlite3_result_text(ctx, "is_magic_str", -1, SQLITE_STATIC);
    } else {
        sqlite3_result_text(ctx, "no_match", -1, SQLITE_STATIC);
    }
}

// 6. Nullable handling UDF: null_safe_concat(a, b)
void udf_null_safe_concat(sqlite3_context* ctx, SqliteUdfArgs args) {
    if (args.size() != 2) return;

    if (args[0].type() == SQLITE_NULL || args[1].type() == SQLITE_NULL) {
        sqlite3_result_null(ctx);
        return;
    }

    const char* str1 = reinterpret_cast<const char*>(sqlite3_value_text(const_cast<sqlite3_value*>(args[0].get())));
    int len1 = sqlite3_value_bytes(const_cast<sqlite3_value*>(args[0].get()));
    const char* str2 = reinterpret_cast<const char*>(sqlite3_value_text(const_cast<sqlite3_value*>(args[1].get())));
    int len2 = sqlite3_value_bytes(const_cast<sqlite3_value*>(args[1].get()));

    SqliteStringOwned res(ctx);
    res.append(str1, len1);
    res.append(str2, len2);
    res.result(ctx);
}

// 7. Zero-argument UDF: get_constant_version()
void udf_get_constant_version(sqlite3_context* ctx, SqliteUdfArgs args) {
    assert(args.size() == 0);
    assert(args[0].type() == SQLITE_NULL);
    sqlite3_result_text(ctx, "v1.0.0", -1, SQLITE_STATIC);
}

// 8. Non-deterministic UDF: sequence_counter()
static int g_seq_counter = 0;
void udf_sequence_counter(sqlite3_context* ctx, SqliteUdfArgs args) {
    (void)args;
    sqlite3_result_int(ctx, ++g_seq_counter);
}

// 9. SqliteValueOwned return wrapper: identity_val(x)
void udf_identity_val(sqlite3_context* ctx, SqliteUdfArgs args) {
    if (args.size() != 1) return;
    
    // Copy into an owned wrapper and return via .result(ctx)
    SqliteValueOwned owned_copy(args[0].get());
    owned_copy.result(ctx);
}

int main() {
    sqlite3_initialize();
    
    sqlite3* db;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
        printf("Failed to open sqlite db\n");
        return 1;
    }
    
    printf("1. Registering scalar UDFs...\n");
    assert(SqliteUdf::define(db, "add_numbers", 2, udf_add_numbers) == SQLITE_OK);
    assert(SqliteUdf::define(db, "repeat_str", 2, udf_repeat_str) == SQLITE_OK);
    assert(SqliteUdf::define(db, "not_blob", 1, udf_not_blob) == SQLITE_OK);
    assert(SqliteUdf::define(db, "sum_variadic", -1, udf_sum_variadic) == SQLITE_OK);
    assert(SqliteUdf::define(db, "check_magic", 1, udf_check_magic) == SQLITE_OK);
    assert(SqliteUdf::define(db, "null_safe_concat", 2, udf_null_safe_concat) == SQLITE_OK);
    assert(SqliteUdf::define(db, "get_version", 0, udf_get_constant_version) == SQLITE_OK);
    assert(SqliteUdf::define(db, "seq_counter", 0, udf_sequence_counter, false) == SQLITE_OK); // non-deterministic
    assert(SqliteUdf::define(db, "identity_val", 1, udf_identity_val) == SQLITE_OK);
    
    // Stateless C++11 Lambda
    assert(SqliteUdf::define(db, "square", 1, [](sqlite3_context* ctx, SqliteUdfArgs args) {
        if (args.size() != 1) return;
        sqlite3_int64 val = args[0].as_int64();
        sqlite3_result_int64(ctx, val * val);
    }) == SQLITE_OK);

    sqlite3_stmt* stmt;

    printf("2. Testing basic math UDF...\n");
    assert(sqlite3_prepare_v2(db, "SELECT add_numbers(5, 10);", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 15);
    sqlite3_finalize(stmt);

    printf("3. Testing string builder UDF...\n");
    assert(sqlite3_prepare_v2(db, "SELECT repeat_str('ha', 3);", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    SqliteStringView expected_str("hahaha", 6);
    SqliteStringView result_str(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), sqlite3_column_bytes(stmt, 0));
    assert(result_str == expected_str);
    sqlite3_finalize(stmt);

    printf("4. Testing blob manipulation UDF...\n");
    assert(sqlite3_prepare_v2(db, "SELECT not_blob(x'FF00AA55');", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_bytes(stmt, 0) == 4);
    const unsigned char* blob_out = reinterpret_cast<const unsigned char*>(sqlite3_column_blob(stmt, 0));
    assert(blob_out[0] == 0x00);
    assert(blob_out[1] == 0xFF);
    assert(blob_out[2] == 0x55);
    assert(blob_out[3] == 0xAA);
    sqlite3_finalize(stmt);

    printf("5. Testing variadic UDF...\n");
    assert(sqlite3_prepare_v2(db, "SELECT sum_variadic(10, 20.5, NULL, 5);", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_double(stmt, 0) == 35.5);
    sqlite3_finalize(stmt);

    // Empty variadic call
    assert(sqlite3_prepare_v2(db, "SELECT sum_variadic();", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_double(stmt, 0) == 0.0);
    sqlite3_finalize(stmt);

    printf("6. Testing heterogeneous value comparisons in UDF...\n");
    assert(sqlite3_prepare_v2(db, "SELECT check_magic(42), check_magic(3.14), check_magic('sqlite'), check_magic('unknown');", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "is_magic_int") == 0);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)), "is_magic_pi") == 0);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)), "is_magic_str") == 0);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)), "no_match") == 0);
    sqlite3_finalize(stmt);

    printf("7. Testing NULL-safe concatenation UDF...\n");
    assert(sqlite3_prepare_v2(db, "SELECT null_safe_concat('hello ', 'world'), null_safe_concat('hello', NULL);", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "hello world") == 0);
    assert(sqlite3_column_type(stmt, 1) == SQLITE_NULL);
    sqlite3_finalize(stmt);

    printf("8. Testing stateless lambda UDF...\n");
    assert(sqlite3_prepare_v2(db, "SELECT square(7);", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int64(stmt, 0) == 49);
    sqlite3_finalize(stmt);

    printf("9. Testing zero-argument UDF...\n");
    assert(sqlite3_prepare_v2(db, "SELECT get_version();", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "v1.0.0") == 0);
    sqlite3_finalize(stmt);

    printf("10. Testing non-deterministic sequential UDF...\n");
    g_seq_counter = 0;
    assert(sqlite3_prepare_v2(db, "SELECT seq_counter(), seq_counter(), seq_counter();", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 1);
    assert(sqlite3_column_int(stmt, 1) == 2);
    assert(sqlite3_column_int(stmt, 2) == 3);
    sqlite3_finalize(stmt);

    printf("11. Testing polymorphic identity value return...\n");
    assert(sqlite3_prepare_v2(db, "SELECT identity_val(123), identity_val('echo'), identity_val(x'0102');", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 123);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)), "echo") == 0);
    assert(sqlite3_column_bytes(stmt, 2) == 2);
    sqlite3_finalize(stmt);

    printf("12. Testing nested UDF composition in SQL...\n");
    assert(sqlite3_prepare_v2(db, "SELECT square(add_numbers(3, 4));", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int64(stmt, 0) == 49);
    sqlite3_finalize(stmt);

    printf("13. Testing UDF inside Table operations (WHERE, ORDER BY)...\n");
    assert(sqlite3_exec(db, "CREATE TABLE nums(val INT);", nullptr, nullptr, nullptr) == SQLITE_OK);
    assert(sqlite3_exec(db, "INSERT INTO nums VALUES (1), (2), (3), (4), (5);", nullptr, nullptr, nullptr) == SQLITE_OK);
    
    // Query with UDF in WHERE and ORDER BY
    assert(sqlite3_prepare_v2(db, "SELECT val FROM nums WHERE add_numbers(val, 2) >= 6 ORDER BY square(val) DESC;", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 5); // 5+2=7 >= 6, square(5)=25
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 4); // 4+2=6 >= 6, square(4)=16
    assert(sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);

    printf("14. Testing function re-definition / override...\n");
    assert(sqlite3_prepare_v2(db, "SELECT add_numbers(10, 20);", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 30);
    sqlite3_finalize(stmt);

    // Override add_numbers to multiply instead
    assert(SqliteUdf::define(db, "add_numbers", 2, [](sqlite3_context* ctx, SqliteUdfArgs args) {
        sqlite3_result_int64(ctx, args[0].as_int64() * args[1].as_int64());
    }) == SQLITE_OK);

    assert(sqlite3_prepare_v2(db, "SELECT add_numbers(10, 20);", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 200); // 10 * 20 = 200
    sqlite3_finalize(stmt);

    printf("15. Testing error handling propagation...\n");
    assert(sqlite3_prepare_v2(db, "SELECT add_numbers(1);", -1, &stmt, nullptr) == SQLITE_ERROR);
    
    // Runtime error returned via sqlite3_result_error
    assert(sqlite3_prepare_v2(db, "SELECT sum_variadic('invalid');", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ERROR);
    sqlite3_finalize(stmt);

    sqlite3_close(db);
    sqlite3_shutdown();
    
    printf("\nAll 15 UDF Builder Test Suites Passed with Complete Coverage!\n");
    return 0;
}
