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
    void *p1 = StateRW::init(db1, [](CounterState *c) { c->counter = 100; });
    {
        CounterState *s = StateRW::from_ptr(p1);
        assert(s != nullptr);
        assert(StateRW::get(db1) == s);
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
    void *p2 = StateTiny::init(db1, [](CounterState *c) { c->counter = 200; });
    {
        CounterState *s = StateTiny::from_ptr(p2);
        assert(s != nullptr);
        assert(StateTiny::get(db1) == s);
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
    void *p3 = StateMutex::init(db1, [](CounterState *c) { c->counter = 300; });
    {
        CounterState *s = StateMutex::from_ptr(p3);
        assert(s != nullptr);
        assert(StateMutex::get(db1) == s);
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
    void *p_rw = SqliteExtStateRw<CounterState>::init(db2, [](CounterState *c) { c->counter = 1; });
    void *p_tiny = SqliteExtStateTiny<CounterState>::init(db2, [](CounterState *c) { c->counter = 2; });
    void *p_mutex = SqliteExtStateMutex<CounterState>::init(db2, [](CounterState *c) { c->counter = 3; });
    {
        CounterState *s_rw = SqliteExtStateRw<CounterState>::from_ptr(p_rw);
        CounterState *s_tiny = SqliteExtStateTiny<CounterState>::from_ptr(p_tiny);
        CounterState *s_mutex = SqliteExtStateMutex<CounterState>::from_ptr(p_mutex);

        assert(s_rw->counter == 1);
        assert(s_tiny->counter == 2);
        assert(s_mutex->counter == 3);
    }

    StateRW::destructor(p1);
    StateTiny::destructor(p2);
    StateMutex::destructor(p3);
    SqliteExtStateRw<CounterState>::destructor(p_rw);
    SqliteExtStateTiny<CounterState>::destructor(p_tiny);
    SqliteExtStateMutex<CounterState>::destructor(p_mutex);

    sqlite3_close(db1);
    sqlite3_close(db2);

    printf("test_lock_policies: PASSED\n");
    return 0;
}
