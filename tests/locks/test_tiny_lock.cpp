#include "sqlite3_tiny_lock.hpp"
#include <stdio.h>
#include <assert.h>

int main() {
    SqliteTinyLock lock;

    // 1. Initial state should be unlocked
    assert(lock.try_lock() == true);
    
    // 2. Trying to lock an already locked mutex should fail
    assert(lock.try_lock() == false); 
    
    // 3. Unlocking should allow it to be locked again
    lock.unlock();
    assert(lock.try_lock() == true);
    lock.unlock();

    // 4. Test RAII Guard (SqliteTinyLockGuard)
    {
        SqliteTinyLockGuard guard(lock);
        assert(lock.try_lock() == false); 
    }
    assert(lock.try_lock() == true);
    lock.unlock();

    // 5. Test Generic SqliteLockGuard<SqliteTinyLock>
    {
        SqliteLockGuard<SqliteTinyLock> generic_guard(lock);
        assert(lock.try_lock() == false);
    }
    assert(lock.try_lock() == true);
    lock.unlock();

    // 6. Test Read/Write Locking interface
    lock.lock_read();
    assert(lock.try_lock() == false);
    lock.unlock_read();

    lock.lock_write();
    assert(lock.try_lock() == false);
    lock.unlock_write();

    // 7. Test Generic Basic Read/Write Guards
    {
        SqliteBasicReadGuard<SqliteTinyLock> read_guard(lock);
        assert(lock.try_lock() == false);
    }
    {
        SqliteBasicWriteGuard<SqliteTinyLock> write_guard(lock);
        assert(lock.try_lock() == false);
    }

    // 8. Verify SqliteLockBase inheritance
    SqliteLockBase* base_ptr = &lock;
    assert(base_ptr != nullptr);

    printf("test_tiny_lock: PASSED\n");
    return 0;
}
