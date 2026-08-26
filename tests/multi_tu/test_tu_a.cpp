#include "test_tu_shared.hpp"
#include <string.h>

static void udf_tu_inc(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    TuAppState* state = ctx.state<TuAppState>();
    if (!state) {
        ctx.result_error("TuAppState not found");
        return;
    }

    int current = 0;
    {
        SqliteExtState<TuAppState>::WriteGuard lock(state);
        lock->counter += 10;
        current = lock->counter;
    }
    ctx.result_int(current);
}

static void udf_tu_set_tag(SqliteContext ctx, SqliteUdfArgs args) {
    if (args.size() != 1) {
        ctx.result_error("udf_tu_set_tag expects 1 argument");
        return;
    }

    TuAppState* state = ctx.state<TuAppState>();
    if (!state) {
        ctx.result_error("TuAppState not found");
        return;
    }

    SqliteStringView str = args[0].as_text();
    {
        SqliteExtState<TuAppState>::WriteGuard lock(state);
        int len = str.length() < 63 ? str.length() : 63;
        memcpy(lock->tag, str.data(), len);
        lock->tag[len] = '\0';
    }
    ctx.result_text("OK", 2, SQLITE_STATIC);
}

void register_tu_a_functions(SqliteDatabaseView db) {
    SqliteExt::define_scalar_with_state<TuAppState, udf_tu_inc>(db, "tu_inc", 0);
    SqliteExt::define_scalar_with_state<TuAppState, udf_tu_set_tag>(db, "tu_set_tag", 1);
}
