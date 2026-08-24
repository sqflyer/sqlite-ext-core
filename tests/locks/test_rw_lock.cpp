#define SQLITE_CORE
#include "sqlite3_rw_lock.hpp"
#include <stdio.h>
#include <assert.h>

void test_c_macros() {
    sqlite3_rw_lock raw_lock;
    
    // 1. Init
    sqlite3_rw_lock_init(&raw_lock);
    
    // 2. Read Acquire/Release
    sqlite3_rw_lock_read_acquire(&raw_lock);
    sqlite3_rw_lock_read_release(&raw_lock);
    
    // 3. Write Acquire/Release
    sqlite3_rw_lock_write_acquire(&raw_lock);
    sqlite3_rw_lock_write_release(&raw_lock);
    
    // 4. Destroy
    sqlite3_rw_lock_destroy(&raw_lock);
}

void test_cpp_class() {
    SqliteRwLock rw_lock;
    
    // Test Read Lock
    rw_lock.lock_read();
    rw_lock.unlock_read();
    
    // Test Write Lock
    rw_lock.lock_write();
    rw_lock.unlock_write();

    // Test Exclusive lock/unlock
    rw_lock.lock();
    rw_lock.unlock();
    
    // Verify native handle exists
    assert(rw_lock.native_handle() != nullptr);

    // Verify SqliteLockBase inheritance
    SqliteLockBase* base_ptr = &rw_lock;
    assert(base_ptr != nullptr);
}

void test_cpp_raii_guards() {
    SqliteRwLock rw_lock;
    
    // Test Read Guard
    {
        SqliteReadGuard read_guard(rw_lock);
        SqliteGuardBase* g_base = &read_guard;
        assert(g_base != nullptr);
    } 
    
    // Test Write Guard
    {
        SqliteWriteGuard write_guard(rw_lock);
        SqliteGuardBase* g_base = &write_guard;
        assert(g_base != nullptr);
    } 

    // Test Generic Lock Guard
    {
        SqliteLockGuard<SqliteRwLock> lock_guard(rw_lock);
        SqliteGuardBase* g_base = &lock_guard;
        assert(g_base != nullptr);
    }
}

int main() {
    test_c_macros();
    test_cpp_class();
    test_cpp_raii_guards();
    
    printf("All RW Lock tests passed successfully!\n");
    return 0;
}
