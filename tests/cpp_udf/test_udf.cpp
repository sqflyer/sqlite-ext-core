#define SQLITE_CORE
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "sqlite3_udf.hpp"

// 1. Basic 2-arg scalar function: add_numbers(a, b) using SqliteContext
void udf_add_numbers(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() != 2) {
        ctx.result_error("add_numbers requires exactly 2 arguments");
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
    ctx.result_int64(result);
}

// 2. String manipulation: repeat_str(str, count) using SqliteContext, SqliteStringOwned & SqliteStringView
void udf_repeat_str(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() != 2) {
        ctx.result_error("repeat_str requires exactly 2 arguments");
        return;
    }

    if (args[0].type() != SQLITE_TEXT || args[1].type() != SQLITE_INTEGER) {
        ctx.result_error("repeat_str expects (TEXT, INTEGER)");
        return;
    }

    SqliteStringView str = args[0].as_text();
    int count = static_cast<int>(args[1].as_int64());

    SqliteStringOwned result(ctx.get());
    for (int i = 0; i < count; i++) {
        result.append(str.data(), str.length());
    }
    
    result.result(ctx);
}

// 3. Blob manipulation: bitwise_not_blob(blob) using SqliteContext
void udf_not_blob(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() != 1 || args[0].type() != SQLITE_BLOB) {
        ctx.result_error("not_blob expects 1 BLOB argument");
        return;
    }

    SqliteBlobView in_blob = args[0].as_blob();
    const unsigned char* raw_data = reinterpret_cast<const unsigned char*>(in_blob.data());
    int len = in_blob.size();
    
    unsigned char inv[64];
    assert(len <= 64);
    for (int i = 0; i < len; i++) {
        inv[i] = ~raw_data[i];
    }
    SqliteBlobOwned inverted(inv, len);
    inverted.result(ctx);
}

// 4. Variadic UDF: sum_variadic(a, b, c, ...) using SqliteContext
void udf_sum_variadic(SqliteContext ctx, SqliteUdfArgs args) {
    double total = 0.0;
    for (int i = 0; i < args.size(); i++) {
        if (args[i].type() == SQLITE_INTEGER) {
            total += args[i].as_int64();
        } else if (args[i].type() == SQLITE_FLOAT) {
            total += args[i].as_double();
        } else if (args[i].type() == SQLITE_NULL) {
            continue;
        } else {
            ctx.result_error("sum_variadic only accepts numbers");
            return;
        }
    }
    ctx.result_double(total);
}

// 5. Heterogeneous comparison inside UDF: check_magic(val) using SqliteContext
void udf_check_magic(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() != 1) {
        ctx.result_error("check_magic requires 1 argument");
        return;
    }

    if (args[0] == 42) {
        ctx.result_text("is_magic_int", -1, SQLITE_STATIC);
    } else if (args[0] == 3.14) {
        ctx.result_text("is_magic_pi", -1, SQLITE_STATIC);
    } else if (args[0] == SqliteStringView("sqlite", 6)) {
        ctx.result_text("is_magic_str", -1, SQLITE_STATIC);
    } else {
        ctx.result_text("no_match", -1, SQLITE_STATIC);
    }
}

// 6. Nullable handling UDF: null_safe_concat(a, b) using SqliteContext
void udf_null_safe_concat(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() != 2) return;

    if (args[0].type() == SQLITE_NULL || args[1].type() == SQLITE_NULL) {
        ctx.result_null();
        return;
    }

    SqliteStringView str1 = args[0].as_text();
    SqliteStringView str2 = args[1].as_text();

    SqliteStringOwned res(ctx.get());
    res.append(str1.data(), str1.length());
    res.append(str2.data(), str2.length());
    res.result(ctx);
}

// 7. Zero-argument UDF: get_constant_version() using SqliteContext
void udf_get_constant_version(SqliteContext ctx, SqliteUdfArgs args) {
    assert(args.size() == 0);
    assert(args[0].type() == SQLITE_NULL);
    ctx.result_text("v1.0.0", -1, SQLITE_STATIC);
}

// 8. Non-deterministic UDF: sequence_counter() using SqliteContext
static int g_seq_counter = 0;
void udf_sequence_counter(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    ctx.result_int(++g_seq_counter);
}

// 9. SqliteValueOwned return wrapper: identity_val(x) using SqliteContext
void udf_identity_val(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() != 1) return;
    
    // Copy into an owned wrapper and return via .result(ctx)
    SqliteValueOwned owned_copy(args[0].get());
    owned_copy.result(ctx);
}

// 10. Raw signature backward-compatibility check: raw_add(a, b)
void udf_raw_add(sqlite3_context* raw_ctx, SqliteUdfArgs args) {
    if (args.size() != 2) return;
    sqlite3_result_int64(raw_ctx, args[0].as_int64() + args[1].as_int64());
}

int main() {
    sqlite3_initialize();
    
    sqlite3* db;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
        printf("Failed to open sqlite db\n");
        return 1;
    }
    
    printf("1. Registering scalar UDFs with SqliteContext...\n");
    assert(SqliteUdf::define(db, "add_numbers", 2, udf_add_numbers) == SQLITE_OK);
    assert(SqliteUdf::define(db, "raw_add", 2, udf_raw_add) == SQLITE_OK);
    assert(SqliteUdf::define(db, "repeat_str", 2, udf_repeat_str) == SQLITE_OK);
    assert(SqliteUdf::define(db, "not_blob", 1, udf_not_blob) == SQLITE_OK);
    assert(SqliteUdf::define(db, "sum_variadic", -1, udf_sum_variadic) == SQLITE_OK);
    assert(SqliteUdf::define(db, "check_magic", 1, udf_check_magic) == SQLITE_OK);
    assert(SqliteUdf::define(db, "null_safe_concat", 2, udf_null_safe_concat) == SQLITE_OK);
    assert(SqliteUdf::define(db, "get_version", 0, udf_get_constant_version) == SQLITE_OK);
    assert(SqliteUdf::define(db, "seq_counter", 0, udf_sequence_counter, false) == SQLITE_OK); // non-deterministic
    assert(SqliteUdf::define(db, "identity_val", 1, udf_identity_val) == SQLITE_OK);
    
    // Stateless C++11 Lambda using SqliteContext
    assert(SqliteUdf::define(db, "square", 1, [](SqliteContext ctx, SqliteUdfArgs args) {
        if (args.size() != 1) return;
        sqlite3_int64 val = args[0].as_int64();
        ctx.result_int64(val * val);
    }) == SQLITE_OK);

    sqlite3_stmt* stmt;

    printf("2. Testing basic math UDF...\n");
    assert(sqlite3_prepare_v2(db, "SELECT add_numbers(5, 10), raw_add(5, 10);", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 15);
    assert(sqlite3_column_int(stmt, 1) == 15);
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

    // Override add_numbers to multiply instead using SqliteContext
    assert(SqliteUdf::define(db, "add_numbers", 2, [](SqliteContext ctx, SqliteUdfArgs args) {
        ctx.result_int64(args[0].as_int64() * args[1].as_int64());
    }) == SQLITE_OK);

    assert(sqlite3_prepare_v2(db, "SELECT add_numbers(10, 20);", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 200); // 10 * 20 = 200
    sqlite3_finalize(stmt);

    printf("15. Testing error handling propagation...\n");
    assert(sqlite3_prepare_v2(db, "SELECT add_numbers(1);", -1, &stmt, nullptr) == SQLITE_ERROR);
    
    // Runtime error returned via SqliteContext result_error
    assert(sqlite3_prepare_v2(db, "SELECT sum_variadic('invalid');", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ERROR);
    sqlite3_finalize(stmt);

    sqlite3_close(db);
    sqlite3_shutdown();
    
    printf("\nAll 15 UDF Builder Test Suites Passed with Complete Coverage!\n");
    return 0;
}
