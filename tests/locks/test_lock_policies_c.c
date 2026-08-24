#define SQLITE_CORE
#include <sqlite3.h>
#include <stdio.h>
#include <assert.h>
#include "sqlite3_ext_state.h"

typedef struct {
    int val;
} StateRWData;

typedef struct {
    int val;
} StateTinyData;

typedef struct {
    int val;
} StateMutexData;

// 1. RW Lock State
SQLITE_EXTENSION_STATE_DECLARE_RW(StateRWData)
SQLITE_EXTENSION_STATE_DEFINE_RW(StateRWData)

// 2. Tiny Lock State
SQLITE_EXTENSION_STATE_DECLARE_TINY(StateTinyData)
SQLITE_EXTENSION_STATE_DEFINE_TINY(StateTinyData)

// 3. Mutex Lock State
SQLITE_EXTENSION_STATE_DECLARE_MUTEX(StateMutexData)
SQLITE_EXTENSION_STATE_DEFINE_MUTEX(StateMutexData)

int main() {
    sqlite3 *db = NULL;
    assert(sqlite3_open(":memory:", &db) == SQLITE_OK);

    // Test RW Lock State
    {
        StateRWData_init(db, NULL, NULL);
        StateRWData *s = StateRWData_from_db(NULL, db);
        assert(s != NULL);
        StateRWData_write_acquire(s);
        s->val = 42;
        StateRWData_write_release(s);

        StateRWData_read_acquire(s);
        assert(s->val == 42);
        StateRWData_read_release(s);
    }

    // Test Tiny Lock State
    {
        StateTinyData_init(db, NULL, NULL);
        StateTinyData *s = StateTinyData_from_db(NULL, db);
        assert(s != NULL);
        StateTinyData_write_acquire(s);
        s->val = 84;
        StateTinyData_write_release(s);

        StateTinyData_read_acquire(s);
        assert(s->val == 84);
        StateTinyData_read_release(s);
    }

    // Test Mutex Lock State
    {
        StateMutexData_init(db, NULL, NULL);
        StateMutexData *s = StateMutexData_from_db(NULL, db);
        assert(s != NULL);
        StateMutexData_write_acquire(s);
        s->val = 126;
        StateMutexData_write_release(s);

        StateMutexData_read_acquire(s);
        assert(s->val == 126);
        StateMutexData_read_release(s);
    }

    sqlite3_close(db);
    printf("test_lock_policies_c: PASSED\n");
    return 0;
}
