#define SQLITE_CORE
#include <sqlite3.h>
#include "sqlite3_mutex_lock.h"
#include <stdio.h>
#include <assert.h>

int main() {
    sqlite3_mutex_lock lock;
    sqlite3_mutex_lock_init(&lock);

    if (lock.handle) {
        // 1. Lock and Unlock
        sqlite3_mutex_lock_lock(&lock);
        sqlite3_mutex_lock_unlock(&lock);

        // 2. Try lock
        assert(sqlite3_mutex_lock_try_lock(&lock) == 1);
        sqlite3_mutex_lock_unlock(&lock);

        // 3. Read/Write acquire adapters
        sqlite3_mutex_lock_read_acquire(&lock);
        sqlite3_mutex_lock_read_release(&lock);

        sqlite3_mutex_lock_write_acquire(&lock);
        sqlite3_mutex_lock_write_release(&lock);
    } else {
        // In single-threaded mode, lock handle may be null and safely handled
        assert(sqlite3_mutex_lock_try_lock(&lock) == 1);
    }

    sqlite3_mutex_lock_destroy(&lock);
    assert(lock.handle == NULL);

    printf("test_mutex_lock_c: PASSED\n");
    return 0;
}
