#include "sqlite3_rw_lock.h"
#include <stdio.h>
#include <assert.h>

int main() {
    sqlite3_rw_lock lock;
    sqlite3_rw_lock_init(&lock);

    // 1. Read acquire and release
    sqlite3_rw_lock_read_acquire(&lock);
    sqlite3_rw_lock_read_release(&lock);

    // 2. Write acquire and release
    sqlite3_rw_lock_write_acquire(&lock);
    sqlite3_rw_lock_write_release(&lock);

    // 3. Destroy
    sqlite3_rw_lock_destroy(&lock);

    printf("test_rw_lock_c: PASSED\n");
    return 0;
}
