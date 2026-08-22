#include "sqlite3_atomic.h"
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

void test_8bit() {
    char state = 0;
    
    // Store & Load
    sqlite_atomic_store_8(&state, 42);
    assert(sqlite_atomic_load_8(&state) == 42);

    // CAS Failure (Weak & Strong)
    char expected = 0;
    int success = sqlite_atomic_cas_strong_8(&state, &expected, 100);
    assert(success == 0); 
    assert(expected == 42); 
    assert(state == 42);    

    expected = 0;
    success = sqlite_atomic_cas_weak_8(&state, &expected, 100);
    assert(success == 0); 
    assert(expected == 42); 

    // CAS Success
    expected = 42; 
    success = sqlite_atomic_cas_strong_8(&state, &expected, 100);
    assert(success == 1); 
    assert(state == 100);   

    // Exchange
    char old = sqlite_atomic_exchange_8(&state, 50);
    assert(old == 100);
    assert(state == 50);

    // Increment / Decrement
    char val = sqlite_atomic_increment_8(&state);
    assert(val == 51);
    assert(state == 51);

    val = sqlite_atomic_decrement_8(&state);
    assert(val == 50);
    assert(state == 50);

    // Fetch Add / Sub
    old = sqlite_atomic_fetch_add_8(&state, 10);
    assert(old == 50);
    assert(state == 60);

    old = sqlite_atomic_fetch_sub_8(&state, 20);
    assert(old == 60);
    assert(state == 40);

    // Bitwise
    old = sqlite_atomic_fetch_or_8(&state, 2);
    assert(old == 40);
    assert(state == 42);

    old = sqlite_atomic_fetch_and_8(&state, 15);
    assert(old == 42);
    assert(state == 10);

    old = sqlite_atomic_fetch_xor_8(&state, 15);
    assert(old == 10);
    assert(state == 5);
}

void test_16bit() {
    short state = 0;
    sqlite_atomic_store_16(&state, 42);
    assert(sqlite_atomic_load_16(&state) == 42);

    short expected = 0;
    int success = sqlite_atomic_cas_strong_16(&state, &expected, 100);
    assert(success == 0); 
    assert(expected == 42); 

    expected = 42; 
    success = sqlite_atomic_cas_strong_16(&state, &expected, 100);
    assert(success == 1); 
    assert(state == 100);   

    short old = sqlite_atomic_exchange_16(&state, 50);
    assert(old == 100);
    assert(state == 50);

    short val = sqlite_atomic_increment_16(&state);
    assert(val == 51);

    val = sqlite_atomic_decrement_16(&state);
    assert(val == 50);

    old = sqlite_atomic_fetch_add_16(&state, 10);
    assert(old == 50);
    assert(state == 60);

    old = sqlite_atomic_fetch_sub_16(&state, 20);
    assert(old == 60);
    assert(state == 40);

    old = sqlite_atomic_fetch_or_16(&state, 2);
    assert(old == 40);
    assert(state == 42);

    old = sqlite_atomic_fetch_and_16(&state, 15);
    assert(old == 42);
    assert(state == 10);

    old = sqlite_atomic_fetch_xor_16(&state, 15);
    assert(old == 10);
    assert(state == 5);
}

void test_32bit() {
    int32_t state = 0;
    sqlite_atomic_store_32(&state, 42);
    assert(sqlite_atomic_load_32(&state) == 42);

    int32_t expected = 0;
    int success = sqlite_atomic_cas_strong_32(&state, &expected, 100);
    assert(success == 0); 
    assert(expected == 42); 

    expected = 42; 
    success = sqlite_atomic_cas_strong_32(&state, &expected, 100);
    assert(success == 1); 
    assert(state == 100);   

    int32_t old = sqlite_atomic_exchange_32(&state, 50);
    assert(old == 100);
    assert(state == 50);

    int32_t val = sqlite_atomic_increment_32(&state);
    assert(val == 51);

    val = sqlite_atomic_decrement_32(&state);
    assert(val == 50);

    old = sqlite_atomic_fetch_add_32(&state, 10);
    assert(old == 50);
    assert(state == 60);

    old = sqlite_atomic_fetch_sub_32(&state, 20);
    assert(old == 60);
    assert(state == 40);

    old = sqlite_atomic_fetch_or_32(&state, 2);
    assert(old == 40);
    assert(state == 42);

    old = sqlite_atomic_fetch_and_32(&state, 15);
    assert(old == 42);
    assert(state == 10);

    old = sqlite_atomic_fetch_xor_32(&state, 15);
    assert(old == 10);
    assert(state == 5);
}

void test_64bit() {
    int64_t state = 0;
    sqlite_atomic_store_64(&state, 42);
    assert(sqlite_atomic_load_64(&state) == 42);

    int64_t expected = 0;
    int success = sqlite_atomic_cas_strong_64(&state, &expected, 100);
    assert(success == 0); 
    assert(expected == 42); 

    expected = 42; 
    success = sqlite_atomic_cas_strong_64(&state, &expected, 100);
    assert(success == 1); 
    assert(state == 100);   

    int64_t old = sqlite_atomic_exchange_64(&state, 50);
    assert(old == 100);
    assert(state == 50);

    int64_t val = sqlite_atomic_increment_64(&state);
    assert(val == 51);

    val = sqlite_atomic_decrement_64(&state);
    assert(val == 50);

    old = sqlite_atomic_fetch_add_64(&state, 10);
    assert(old == 50);
    assert(state == 60);

    old = sqlite_atomic_fetch_sub_64(&state, 20);
    assert(old == 60);
    assert(state == 40);

    old = sqlite_atomic_fetch_or_64(&state, 2);
    assert(old == 40);
    assert(state == 42);

    old = sqlite_atomic_fetch_and_64(&state, 15);
    assert(old == 42);
    assert(state == 10);

    old = sqlite_atomic_fetch_xor_64(&state, 15);
    assert(old == 10);
    assert(state == 5);
}

void test_ptr() {
    void* state = NULL;
    void* a = (void*)1;
    void* b = (void*)2;
    void* c = (void*)3;
    
    // Store & Load
    sqlite_atomic_store_ptr(&state, a);
    assert(sqlite_atomic_load_ptr(&state) == a);

    // CAS Failure
    void* expected = NULL;
    int success = sqlite_atomic_cas_strong_ptr(&state, &expected, b);
    assert(success == 0);
    assert(expected == a);
    
    // CAS Success
    expected = a;
    success = sqlite_atomic_cas_strong_ptr(&state, &expected, b);
    assert(success == 1);
    assert(state == b);

    // Exchange
    void* old = sqlite_atomic_exchange_ptr(&state, c);
    assert(old == b);
    assert(state == c);
}

int main() {
    test_8bit();
    test_16bit();
    test_32bit();
    test_64bit();
    test_ptr();
    
    printf("test_atomic: PASSED\n");
    return 0;
}
