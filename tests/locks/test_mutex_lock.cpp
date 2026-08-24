#define SQLITE_CORE
#include "sqlite3_mutex_lock.hpp"
#include <stdio.h>
#include <assert.h>

int main() {
    // 1. Test standard SqliteMutex wrapper 
    // This calls the actual sqlite3_mutex_alloc backend from the linked SQLite library.
    {
        SqliteMutex mutex; 
        
        mutex.lock();
        mutex.unlock();
        
        if (mutex.native_handle() != nullptr) {
            assert(mutex.try_lock() == true); 
            mutex.unlock(); // Unlock it after a successful try_lock
        } else {
            assert(mutex.try_lock() == true);
        }
        
        // Test SqliteMutexGuard
        {
            SqliteMutexGuard guard(mutex);
        } // Unlocks automatically

        // Test generic SqliteLockGuard<SqliteMutex>
        {
            SqliteLockGuard<SqliteMutex> guard(mutex);
        }

        // Test read/write methods and basic guards
        mutex.lock_read();
        mutex.unlock_read();
        mutex.lock_write();
        mutex.unlock_write();

        {
            SqliteBasicReadGuard<SqliteMutex> read_guard(mutex);
        }
        {
            SqliteBasicWriteGuard<SqliteMutex> write_guard(mutex);
        }

        // Verify SqliteLockBase inheritance
        SqliteLockBase* base_ptr = &mutex;
        assert(base_ptr != nullptr);
    }

    // 2. Test single-threaded null safety explicitly
    {
        sqlite3_mutex* null_mutex = nullptr;
        SqliteMutexGuard guard(null_mutex); 
    }

    // 3. Test Pure C sqlite3_mutex_lock API
    {
        sqlite3_mutex_lock c_mutex;
        sqlite3_mutex_lock_init(&c_mutex);
        if (c_mutex.handle) {
            sqlite3_mutex_lock_lock(&c_mutex);
            sqlite3_mutex_lock_unlock(&c_mutex);
            assert(sqlite3_mutex_lock_try_lock(&c_mutex) == 1);
            sqlite3_mutex_lock_unlock(&c_mutex);
            sqlite3_mutex_lock_read_acquire(&c_mutex);
            sqlite3_mutex_lock_read_release(&c_mutex);
            sqlite3_mutex_lock_write_acquire(&c_mutex);
            sqlite3_mutex_lock_write_release(&c_mutex);
        }
        sqlite3_mutex_lock_destroy(&c_mutex);
    }

    printf("test_mutex_lock: PASSED\n");
    return 0;
}
