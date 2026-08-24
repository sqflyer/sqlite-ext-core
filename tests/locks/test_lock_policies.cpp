#define SQLITE_CORE
#include <sqlite3.h>
#include "sqlite3_ext_state.hpp"
#include <stdio.h>
#include <assert.h>

struct CounterState {
    int counter;
};

// 1. RW Lock Policy State
using StateRW = SqliteExtState<CounterState, SqliteRwLock>;

// 2. Tiny Lock Policy State
using StateTiny = SqliteExtState<CounterState, SqliteTinyLock>;

// 3. Mutex Lock Policy State
using StateMutex = SqliteExtState<CounterState, SqliteMutex>;

int main() {
    sqlite3 *db1 = nullptr;
    sqlite3 *db2 = nullptr;
    assert(sqlite3_open(":memory:", &db1) == SQLITE_OK);
    assert(sqlite3_open(":memory:", &db2) == SQLITE_OK);

    // Test 1: SqliteExtState with SqliteRwLock (Default)
    {
        CounterState *s = StateRW::get_or_create(db1, [](CounterState *c) { c->counter = 100; });
        assert(s != nullptr);
        {
            StateRW::WriteGuard w(s);
            w->counter += 50;
        }
        {
            StateRW::ReadGuard r(s);
            assert(r->counter == 150);
        }
    }

    // Test 2: SqliteExtState with SqliteTinyLock (1-byte Spinlock)
    {
        CounterState *s = StateTiny::get_or_create(db1, [](CounterState *c) { c->counter = 200; });
        assert(s != nullptr);
        {
            StateTiny::WriteGuard w(s);
            w->counter += 25;
        }
        {
            StateTiny::ReadGuard r(s);
            assert(r->counter == 225);
        }
    }

    // Test 3: SqliteExtState with SqliteMutex (SQLite Native Mutex)
    {
        CounterState *s = StateMutex::get_or_create(db1, [](CounterState *c) { c->counter = 300; });
        assert(s != nullptr);
        {
            StateMutex::WriteGuard w(s);
            w->counter += 10;
        }
        {
            StateMutex::ReadGuard r(s);
            assert(r->counter == 310);
        }
    }

    // Test 4: Verify type aliases
    {
        CounterState *s_rw = SqliteExtStateRw<CounterState>::get_or_create(db2, [](CounterState *c) { c->counter = 1; });
        CounterState *s_tiny = SqliteExtStateTiny<CounterState>::get_or_create(db2, [](CounterState *c) { c->counter = 2; });
        CounterState *s_mutex = SqliteExtStateMutex<CounterState>::get_or_create(db2, [](CounterState *c) { c->counter = 3; });

        assert(s_rw->counter == 1);
        assert(s_tiny->counter == 2);
        assert(s_mutex->counter == 3);
    }

    sqlite3_close(db1);
    sqlite3_close(db2);

    printf("test_lock_policies: PASSED\n");
    return 0;
}
