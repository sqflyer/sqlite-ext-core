#include "sqlite3_tiny_lock.h"
#include <stdio.h>
#include <assert.h>

int main() {
    sqlite3_tiny_lock lock;
    sqlite3_tiny_lock_init(&lock);

    // 1. Initial state should be unlocked
    assert(sqlite3_tiny_lock_try_lock(&lock) == 1);

    // 2. Trying to lock an already locked lock should fail
    assert(sqlite3_tiny_lock_try_lock(&lock) == 0);

    // 3. Unlock and relock
    sqlite3_tiny_lock_unlock(&lock);
    assert(sqlite3_tiny_lock_try_lock(&lock) == 1);
    sqlite3_tiny_lock_unlock(&lock);

    // 4. Test blocking lock and unlock
    sqlite3_tiny_lock_lock(&lock);
    assert(sqlite3_tiny_lock_try_lock(&lock) == 0);
    sqlite3_tiny_lock_unlock(&lock);

    // 5. Test read/write acquire/release adapters
    sqlite3_tiny_lock_read_acquire(&lock);
    assert(sqlite3_tiny_lock_try_lock(&lock) == 0);
    sqlite3_tiny_lock_read_release(&lock);

    sqlite3_tiny_lock_write_acquire(&lock);
    assert(sqlite3_tiny_lock_try_lock(&lock) == 0);
    sqlite3_tiny_lock_write_release(&lock);

    // 6. Test destroy
    sqlite3_tiny_lock_destroy(&lock);

    printf("test_tiny_lock_c: PASSED\n");
    return 0;
}
