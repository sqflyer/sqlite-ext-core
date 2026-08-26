#include "test_tu_shared.hpp"
#include <string.h>
#include <stdio.h>

static void udf_tu_get_stats(SqliteContext ctx, SqliteUdfArgs args) {
    (void)args;
    TuAppState* state = ctx.state<TuAppState>();
    if (!state) {
        ctx.result_error("TuAppState not found in TU-B");
        return;
    }

    int c = 0;
    char buf[128];
    {
        SqliteExtState<TuAppState>::ReadGuard lock(state);
        c = lock->counter;
        snprintf(buf, sizeof(buf), "counter=%d,tag=%s", c, lock->tag);
    }
    ctx.result_text(buf, static_cast<int>(strlen(buf)), SQLITE_TRANSIENT);
}

void register_tu_b_functions(SqliteDatabaseView db) {
    SqliteExt::define_scalar_with_state<TuAppState, udf_tu_get_stats>(db, "tu_get_stats", 0);
}
