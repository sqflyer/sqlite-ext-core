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

    // 4. Test RAII Guard
    {
        SqliteTinyLockGuard guard(lock);
        // Lock should be held by the guard
        assert(lock.try_lock() == false); 
    }
    
    // 5. Guard went out of scope, should be unlocked
    assert(lock.try_lock() == true);
    lock.unlock();

    printf("test_tiny_lock: PASSED\n");
    return 0;
}
