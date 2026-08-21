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
        
        mutex.unlock();
        
        // Test Guard
        {
            SqliteMutexGuard guard(mutex);
        } // Unlocks automatically
    }

    // 2. Test single-threaded null safety explicitly
    {
        // Even if SQLite is multi-threaded, we can manually pass a nullptr
        // to the Guard to prove that the C++ wrapper safely no-ops it without segfaulting.
        sqlite3_mutex* null_mutex = nullptr;
        SqliteMutexGuard guard(null_mutex); 
    }

    printf("test_mutex_lock: PASSED\n");
    return 0;
}
