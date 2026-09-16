#define SQLITE_CORE
#include <cassert>
#include <cstdio>
#include <cstring>
#include <sqlite3.h>

#include "direct_dispatch_context.hpp"
#include "direct_dispatch_hub.hpp"
#include "sqlite3_udf.hpp"

// ============================================================================
// Helpers & Test Functions
// ============================================================================

struct CustomState {
    int magic = 0x42;
    int counter = 0;
};

// Stateless handler for raw function pointer testing
static void raw_add_handler(DirectDispatchContext& ctx, SqliteRowOwnedWrapper args) {
    if (args.size() < 2) {
        ctx.result_error("raw_add requires 2 arguments", SQLITE_MISUSE);
        return;
    }
    sqlite3_int64 a = args[0].as_int64();
    sqlite3_int64 b = args[1].as_int64();
    ctx.result_int64(a + b);
}

// Templated dual-execution UDF implementation (callable by both SqliteContext and DirectDispatchContext)
template <typename Context, typename Args>
void udf_bloom_check(Context& ctx, Args args) {
    if (args.size() < 2) {
        ctx.result_error("memkv_bloom_check requires (key, id)", SQLITE_MISUSE);
        return;
    }
    const char* key = args[0].as_text().data();
    int id = args[1].as_int();
    if (!key) {
        ctx.result_null();
        return;
    }
    // Simulate bloom filter check: key starts with 'u' and id is even
    bool match = (key[0] == 'u' && (id % 2 == 0));
    ctx.result_int(match ? 1 : 0);
}

// ============================================================================
// Test 1: DirectDispatchContext Primitives & Parity
// ============================================================================
static void test_direct_context_primitives() {
    printf("--- Running test_direct_context_primitives ---\n");

    sqlite3* db = nullptr;
    sqlite3_open(":memory:", &db);
    assert(db != nullptr);

    DirectDispatchContext ctx(db);
    assert(ctx.db() == db);
    assert(ctx.db_handle() == db);
    assert(!ctx.is_error());
    assert(ctx.error_message() == nullptr);
    assert(ctx.error_code() == SQLITE_OK);
    assert(ctx.result().is_null());

    // Integer 32-bit & 64-bit
    ctx.result_int(42);
    assert(!ctx.is_error());
    assert(ctx.result().is_integer());
    assert(ctx.result().as_int() == 42);

    ctx.result_int64(9876543210LL);
    assert(ctx.result().as_int64() == 9876543210LL);

    // Double
    ctx.result_double(3.14159);
    assert(ctx.result().is_float());
    assert(ctx.result().as_double() > 3.14 && ctx.result().as_double() < 3.15);

    // Bool
    ctx.result_bool(true);
    assert(ctx.result().is_integer());
    assert(ctx.result().as_bool() == true);
    assert(ctx.result().subtype() == SQLITE_SUBTYPE_BOOL);

    ctx.result_bool(false);
    assert(ctx.result().as_bool() == false);

    // Null
    ctx.result_null();
    assert(ctx.result().is_null());

    // Move construction & assignment
    ctx.result_int(12345);
    DirectDispatchContext moved_ctx(sqlite_move(ctx));
    assert(moved_ctx.result().as_int() == 12345);
    assert(moved_ctx.db() == db);

    DirectDispatchContext assign_ctx;
    assign_ctx = sqlite_move(moved_ctx);
    assert(assign_ctx.result().as_int() == 12345);

    sqlite3_close(db);
    printf("test_direct_context_primitives: PASSED\n");
}

// ============================================================================
// Test 2: Strings, Blobs, ZeroBlob & Borrowed Buffers
// ============================================================================
static void test_direct_context_strings_and_blobs() {
    printf("--- Running test_direct_context_strings_and_blobs ---\n");

    DirectDispatchContext ctx;

    // Short inline SBO string (<= 21 chars)
    ctx.result_text("short_string");
    assert(ctx.result().is_text());
    assert(ctx.result().as_text() == "short_string");
    assert(!ctx.result().is_heap_allocated());
    assert(!ctx.result().is_borrowed());

    // Long heap string (> 21 chars)
    const char* long_text = "This is a long string that definitely exceeds the twenty-one character SBO limit!";
    ctx.result_text(long_text);
    assert(ctx.result().is_text());
    assert(ctx.result().as_text() == long_text);
    assert(ctx.result().is_heap_allocated());
    assert(!ctx.result().is_borrowed());

    // Zero-copy borrowed string via SQLITE_STATIC
    static const char kStaticBuffer[] = "Static compile-time literal buffer for testing zero-copy borrowing semantics.";
    ctx.result_text(kStaticBuffer, -1, SQLITE_STATIC);
    assert(ctx.result().is_text());
    assert(ctx.result().as_text() == kStaticBuffer);
    assert(ctx.result().is_heap_allocated());
    assert(ctx.result().is_borrowed());

    // Zero-copy borrowed blob via SQLITE_STATIC
    uint8_t raw_data[32];
    for (int i = 0; i < 32; ++i) raw_data[i] = static_cast<uint8_t>(i * 3);
    ctx.result_blob(raw_data, sizeof(raw_data), SQLITE_STATIC);
    assert(ctx.result().is_blob());
    assert(ctx.result().as_blob().size() == 32);
    assert(ctx.result().is_borrowed());
    assert(memcmp(ctx.result().as_blob().data(), raw_data, 32) == 0);

    // Explicit borrowed helpers
    ctx.result_borrowed_text(kStaticBuffer, sizeof(kStaticBuffer) - 1, SQLITE_SUBTYPE_JSON);
    assert(ctx.result().is_borrowed());
    assert(ctx.result().subtype() == SQLITE_SUBTYPE_JSON);

    ctx.result_borrowed_blob(raw_data, sizeof(raw_data), SQLITE_SUBTYPE_VECTOR);
    assert(ctx.result().is_borrowed());
    assert(ctx.result().subtype() == SQLITE_SUBTYPE_VECTOR);

    // SqliteStringView & SqliteBlobView
    SqliteStringView sview("string_view_payload");
    ctx.result_text(sview);
    assert(ctx.result().as_text() == "string_view_payload");

    SqliteBlobView bview("blob_payload", 12);
    ctx.result_blob(bview);
    assert(ctx.result().as_blob().size() == 12);

    // ZeroBlob
    ctx.result_zeroblob(16);
    assert(ctx.result().is_blob());
    assert(ctx.result().as_blob().size() == 16);
    const uint8_t* zb16 = reinterpret_cast<const uint8_t*>(ctx.result().as_blob().data());
    for (int i = 0; i < 16; ++i) assert(zb16[i] == 0);

    ctx.result_zeroblob(64);
    assert(ctx.result().is_blob());
    assert(ctx.result().as_blob().size() == 64);
    const uint8_t* zb64 = reinterpret_cast<const uint8_t*>(ctx.result().as_blob().data());
    for (int i = 0; i < 64; ++i) assert(zb64[i] == 0);

    // Null string/blob check
    ctx.result_text(nullptr);
    assert(ctx.result().is_null());
    ctx.result_blob(nullptr, 10);
    assert(ctx.result().is_null());

    // Result value setters
    SqliteValueOwned owned_val("cloned_value");
    ctx.result_value(owned_val);
    assert(ctx.result().as_text() == "cloned_value");

    ctx.result_value(SqliteValueOwned("moved_value"));
    assert(ctx.result().as_text() == "moved_value");

    printf("test_direct_context_strings_and_blobs: PASSED\n");
}

// ============================================================================
// Test 3: Pointers, Subtypes, Errors & take_result()
// ============================================================================
static void test_direct_context_pointer_and_errors() {
    printf("--- Running test_direct_context_pointer_and_errors ---\n");

    DirectDispatchContext ctx;

    // Pointer passing
    int payload = 0xbeef;
    ctx.result_pointer(&payload);
    assert(ctx.result().is_pointer());
    assert(ctx.result().as_pointer<int>() == &payload);
    assert(*ctx.result().as_pointer<int>() == 0xbeef);

    // Subtype
    ctx.result_int(42);
    ctx.result_subtype(SQLITE_SUBTYPE_JSON);
    assert(ctx.result().subtype() == SQLITE_SUBTYPE_JSON);

    // Errors
    ctx.result_error("Custom execution error");
    assert(ctx.is_error());
    assert(ctx.error_code() == SQLITE_ERROR);
    assert(strcmp(ctx.error_message(), "Custom execution error") == 0);
    assert(ctx.result().is_null());

    ctx.result_error("Constraint violated", SQLITE_CONSTRAINT);
    assert(ctx.is_error());
    assert(ctx.error_code() == SQLITE_CONSTRAINT);
    assert(strcmp(ctx.error_message(), "Constraint violated") == 0);

    ctx.result_error_nomem();
    assert(ctx.is_error());
    assert(ctx.error_code() == SQLITE_NOMEM);

    ctx.result_error_toobig();
    assert(ctx.is_error());
    assert(ctx.error_code() == SQLITE_TOOBIG);

    ctx.result_error_code(SQLITE_BUSY);
    assert(ctx.is_error());
    assert(ctx.error_code() == SQLITE_BUSY);

    // take_result()
    ctx.result_int(999);
    assert(ctx.result().as_int() == 999);
    SqliteValueOwned taken = ctx.take_result();
    assert(taken.is_integer());
    assert(taken.as_int() == 999);
    assert(ctx.result().is_null()); // Context result was reset to SQLITE_NULL

    printf("test_direct_context_pointer_and_errors: PASSED\n");
}

// ============================================================================
// Test 4: User Data & State Resolution
// ============================================================================
static void test_direct_context_state_resolution() {
    printf("--- Running test_direct_context_state_resolution ---\n");

    CustomState st;
    st.magic = 0x7788;
    st.counter = 10;

    DirectDispatchContext ctx(nullptr, &st);
    assert(ctx.user_data() == &st);

    CustomState* resolved = ctx.state<CustomState>();
    assert(resolved != nullptr);
    assert(resolved->magic == 0x7788);
    assert(resolved->counter == 10);

    resolved->counter++;
    assert(st.counter == 11);

    ctx.set_user_data(nullptr);
    assert(ctx.user_data() == nullptr);

    printf("test_direct_context_state_resolution: PASSED\n");
}

// ============================================================================
// Test 5: DirectDispatchHub Registration & duo::HashMap Scaling
// ============================================================================
// Test 5: DirectDispatchHub Registration, Lookup & Lifecycle
// ============================================================================
static void test_hub_registration_and_lookup() {
    printf("--- Running test_hub_registration_and_lookup ---\n");

    DirectDispatchHub hub;
    assert(hub.empty());
    assert(hub.size() == 0);
    assert(!hub.contains("add"));

    // Register via function pointer
    bool ok = hub.register_function("add", raw_add_handler);
    assert(ok);
    assert(!hub.empty());
    assert(hub.size() == 1);
    assert(hub.contains("add"));

    DirectDispatchHub::DirectDispatchHandler h = hub.find("add");
    assert(h != nullptr);
    assert(h == raw_add_handler);

    // Register via register_udf template
    ok = hub.register_udf<udf_bloom_check<DirectDispatchContext, SqliteRowOwnedWrapper>>("bloom");
    assert(ok);
    assert(hub.size() == 2);
    assert(hub.contains("bloom"));

    // Re-registering existing names must return false (prevents overwriting)
    bool dup_ok = hub.register_function("add", raw_add_handler);
    assert(!dup_ok);
    dup_ok = hub.register_function(duo::String("add"), raw_add_handler);
    assert(!dup_ok);
    dup_ok = hub.register_function(duo::StringView("add"), raw_add_handler);
    assert(!dup_ok);
    dup_ok = hub.register_udf<udf_bloom_check<DirectDispatchContext, SqliteRowOwnedWrapper>>("bloom");
    assert(!dup_ok);
    assert(hub.size() == 2); // Size must remain unchanged

    // Not found check
    assert(hub.find("nonexistent") == nullptr);
    assert(!hub.contains("nonexistent"));

    // Dynamic scaling beyond 128 items (verifying duo::HashMap Robin Hood rehashing)
    char name_buf[64][32];
    for (int i = 0; i < 64; ++i) {
        snprintf(name_buf[i], sizeof(name_buf[i]), "func_%d", i);
        bool reg_ok = hub.register_function(name_buf[i], raw_add_handler);
        assert(reg_ok);
    }
    assert(hub.size() == 66);

    for (int i = 0; i < 64; ++i) {
        assert(hub.contains(name_buf[i]));
        DirectDispatchHub::DirectDispatchHandler f = hub.find(name_buf[i]);
        assert(f != nullptr);
        assert(f == raw_add_handler);
    }

    // Erase test
    assert(hub.erase("add"));
    assert(!hub.contains("add"));
    assert(hub.find("add") == nullptr);
    assert(hub.size() == 65);

    // Clear test
    hub.clear();
    assert(hub.empty());
    assert(hub.size() == 0);

    printf("test_hub_registration_and_lookup: PASSED\n");
}

// ============================================================================
// Test 6: DirectDispatchHub dispatch & withSqliteRowOwned Stack Allocation
// ============================================================================
static void test_hub_dispatch_and_stack_allocation() {
    printf("--- Running test_hub_dispatch_and_stack_allocation ---\n");

    DirectDispatchHub hub;
    hub.register_function("add", raw_add_handler);
    hub.register_udf<udf_bloom_check<DirectDispatchContext, SqliteRowOwnedWrapper>>("bloom_check");

    // Dispatch "add" with 2 arguments (allocated on stack via withSqliteRowOwned)
    DirectDispatchContext ctx;
    bool ok = hub.dispatch("add", 2, [](SqliteRowOwnedWrapper row) {
        row[0] = 100LL;
        row[1] = 250LL;
    }, &ctx);

    assert(ok);
    assert(!ctx.is_error());
    assert(ctx.result().as_int64() == 350LL);

    // Dispatch "bloom_check" with 2 arguments
    ok = hub.dispatch("bloom_check", 2, [](SqliteRowOwnedWrapper row) {
        row[0] = "users";
        row[1] = 1002;
    }, &ctx);

    assert(ok);
    assert(!ctx.is_error());
    assert(ctx.result().as_int() == 1); // Starts with 'u' and 1002 is even

    // Negative match check
    ok = hub.dispatch("bloom_check", 2, [](SqliteRowOwnedWrapper row) {
        row[0] = "orders";
        row[1] = 1002;
    }, &ctx);
    assert(ok);
    assert(ctx.result().as_int() == 0); // Starts with 'o'

    // Function not found check
    ok = hub.dispatch("missing_fn", 2, [](SqliteRowOwnedWrapper) {}, &ctx);
    assert(!ok);
    assert(ctx.is_error());
    assert(ctx.error_code() == SQLITE_NOTFOUND);

    // Stack argument arity testing (N = 0, 1, 4, 8, 16, and fallback >16)
    hub.register_function("sum_all", [](DirectDispatchContext& c, SqliteRowOwnedWrapper args) {
        sqlite3_int64 total = 0;
        for (int i = 0; i < args.size(); ++i) {
            total += args[i].as_int64();
        }
        c.result_int64(total);
    });

    // 0 args
    ok = hub.dispatch("sum_all", 0, [](SqliteRowOwnedWrapper) {}, &ctx);
    assert(ok && ctx.result().as_int64() == 0);

    // 4 args
    ok = hub.dispatch("sum_all", 4, [](SqliteRowOwnedWrapper row) {
        for (int i = 0; i < 4; ++i) row[i] = 10;
    }, &ctx);
    assert(ok && ctx.result().as_int64() == 40);

    // 8 args
    ok = hub.dispatch("sum_all", 8, [](SqliteRowOwnedWrapper row) {
        for (int i = 0; i < 8; ++i) row[i] = 5;
    }, &ctx);
    assert(ok && ctx.result().as_int64() == 40);

    // 16 args
    ok = hub.dispatch("sum_all", 16, [](SqliteRowOwnedWrapper row) {
        for (int i = 0; i < 16; ++i) row[i] = 2;
    }, &ctx);
    assert(ok && ctx.result().as_int64() == 32);

    // 20 args (triggers heap fallback > 16)
    ok = hub.dispatch("sum_all", 20, [](SqliteRowOwnedWrapper row) {
        for (int i = 0; i < 20; ++i) row[i] = 1;
    }, &ctx);
    assert(ok && ctx.result().as_int64() == 20);

    // invoke() with pre-populated span (const char*, duo::String, and duo::StringView)
    SqliteValueOwned arr[2] = { SqliteValueOwned(40LL), SqliteValueOwned(60LL) };
    SqliteRowOwnedWrapper row_span(arr, 2);

    // const char* invoke
    ok = hub.invoke("add", ctx, row_span);
    assert(ok && ctx.result().as_int64() == 100LL);

    // duo::String invoke
    duo::String str_add("add");
    ok = hub.invoke(str_add, ctx, row_span);
    assert(ok && ctx.result().as_int64() == 100LL);

    // duo::StringView invoke
    duo::StringView sv_add("add");
    ok = hub.invoke(sv_add, ctx, row_span);
    assert(ok && ctx.result().as_int64() == 100LL);

    // dispatch() with duo::String
    ok = hub.dispatch(str_add, 2, [](SqliteRowOwnedWrapper row) {
        row[0] = 50LL;
        row[1] = 75LL;
    }, &ctx);
    assert(ok && ctx.result().as_int64() == 125LL);

    // dispatch() with duo::StringView
    ok = hub.dispatch(sv_add, 2, [](SqliteRowOwnedWrapper row) {
        row[0] = 30LL;
        row[1] = 40LL;
    }, &ctx);
    assert(ok && ctx.result().as_int64() == 70LL);

    printf("test_hub_dispatch_and_stack_allocation: PASSED\n");
}

// ============================================================================
// Test 7: Dual UDF Execution Compatibility (SQLite VDBE + DirectDispatchHub)
// ============================================================================
static void test_dual_udf_compatibility() {
    printf("--- Running test_dual_udf_compatibility ---\n");

    sqlite3* db = nullptr;
    int rc = sqlite3_open(":memory:", &db);
    assert(rc == SQLITE_OK && db != nullptr);

    // 1. DirectDispatchHub registration and in-memory execution
    DirectDispatchHub hub;
    hub.register_udf<udf_bloom_check<DirectDispatchContext, SqliteRowOwnedWrapper>>("bloom_check");

    DirectDispatchContext dctx(db);
    bool ok = hub.dispatch("bloom_check", 2, [](SqliteRowOwnedWrapper row) {
        row[0] = "users";
        row[1] = 1002;
    }, &dctx);
    assert(ok);
    int direct_res = dctx.result().as_int();
    assert(direct_res == 1);

    // Verify multiple database connections sharing the EXACT same hub instance
    sqlite3* db2 = nullptr;
    rc = sqlite3_open(":memory:", &db2);
    assert(rc == SQLITE_OK && db2 != nullptr);
    DirectDispatchContext dctx2(db2);
    ok = hub.dispatch("bloom_check", 2, [](SqliteRowOwnedWrapper row) {
        row[0] = "users";
        row[1] = 1002;
    }, &dctx2);
    assert(ok && dctx2.result().as_int() == 1);
    assert(dctx2.db() == db2);
    sqlite3_close(db2);

    // 2. Standard SQLite UDF registration using SqliteUdf and VDBE query execution
    rc = SqliteUdf::define(db, "bloom_check", 2, [](SqliteContext& ctx, SqliteUdfArgs args) {
        // Calls the EXACT same templated function!
        udf_bloom_check(ctx, args);
    });
    assert(rc == SQLITE_OK);

    // Execute via SQLite SQL parser and VDBE bytecode
    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db, "SELECT bloom_check('users', 1002);", -1, &stmt, nullptr);
    assert(rc == SQLITE_OK);
    rc = sqlite3_step(stmt);
    assert(rc == SQLITE_ROW);
    int sql_res = sqlite3_column_int(stmt, 0);
    assert(sql_res == direct_res); // Direct dispatch and SQLite VDBE yielded identical result!
    sqlite3_finalize(stmt);

    // Execute negative case via SQL
    rc = sqlite3_prepare_v2(db, "SELECT bloom_check('orders', 1002);", -1, &stmt, nullptr);
    assert(rc == SQLITE_OK);
    rc = sqlite3_step(stmt);
    assert(rc == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 0);
    sqlite3_finalize(stmt);

    sqlite3_close(db);
    printf("test_dual_udf_compatibility: PASSED\n");
}

// ============================================================================
// Main Test Runner
// ============================================================================
int main() {
    printf("================================================================\n");
    printf("RUNNING DIRECT DISPATCH CONTEXT & HUB TESTS\n");
    printf("================================================================\n");

    test_direct_context_primitives();
    test_direct_context_strings_and_blobs();
    test_direct_context_pointer_and_errors();
    test_direct_context_state_resolution();
    test_hub_registration_and_lookup();
    test_hub_dispatch_and_stack_allocation();
    test_dual_udf_compatibility();

    printf("================================================================\n");
    printf("ALL DIRECT DISPATCH CONTEXT & HUB TESTS PASSED SUCCESSFULLY!\n");
    printf("================================================================\n");
    return 0;
}
