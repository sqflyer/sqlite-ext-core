#include "sqlite3_atomic.h"
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

int main() {
    int32_t state = 0;
    
    // 1. Test Store
    SQLITE_ATOMIC_STORE_32(&state, 42);
    assert(state == 42);

    // 2. Test CAS Failure
    int32_t expected = 0;
    int success = SQLITE_ATOMIC_CAS_STRONG_32(&state, &expected, 100);
    assert(success == 0); // Should fail
    assert(expected == 42); // Should have captured the actual state (TOCTOU fix verification)
    assert(state == 42);    // State should be unchanged

    // 3. Test CAS Success
    expected = 42; // Now expected matches memory
    success = SQLITE_ATOMIC_CAS_STRONG_32(&state, &expected, 100);
    assert(success == 1); // Should succeed
    assert(state == 100);   // State should be updated

    printf("test_atomic: PASSED\n");
    return 0;
}
