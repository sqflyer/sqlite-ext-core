#include "sqlite3_atomic.hpp"
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

void test_8bit() {
    int8_t state = 0;
    
    // Store & Load
    sqlite_atomic_store(&state, (int8_t)42);
    assert(sqlite_atomic_load(&state) == 42);

    // CAS Failure
    int8_t expected = 0;
    int success = sqlite_atomic_cas_strong(&state, &expected, (int8_t)100);
    assert(success == 0); 
    assert(expected == 42); 

    // CAS Success
    expected = 42; 
    success = sqlite_atomic_cas_strong(&state, &expected, (int8_t)100);
    assert(success == 1); 
    assert(state == 100);   

    // Exchange
    int8_t old = sqlite_atomic_exchange(&state, (int8_t)50);
    assert(old == 100);
    assert(state == 50);

    // Increment / Decrement
    int8_t val = sqlite_atomic_increment(&state);
    assert(val == 51);

    val = sqlite_atomic_decrement(&state);
    assert(val == 50);

    // Arithmetic
    old = sqlite_atomic_fetch_add(&state, (int8_t)10);
    assert(old == 50);
    assert(state == 60);

    old = sqlite_atomic_fetch_sub(&state, (int8_t)20);
    assert(old == 60);
    assert(state == 40);

    // Bitwise
    old = sqlite_atomic_fetch_or(&state, (int8_t)2);
    assert(old == 40);
    assert(state == 42);

    old = sqlite_atomic_fetch_and(&state, (int8_t)15);
    assert(old == 42);
    assert(state == 10);

    old = sqlite_atomic_fetch_xor(&state, (int8_t)15);
    assert(old == 10);
    assert(state == 5);
}

void test_32bit() {
    int32_t state = 0;
    
    // Store & Load
    sqlite_atomic_store(&state, (int32_t)42);
    assert(sqlite_atomic_load(&state) == 42);

    // CAS Failure
    int32_t expected = 0;
    int success = sqlite_atomic_cas_strong(&state, &expected, (int32_t)100);
    assert(success == 0); 
    assert(expected == 42); 

    // CAS Success
    expected = 42; 
    success = sqlite_atomic_cas_strong(&state, &expected, (int32_t)100);
    assert(success == 1); 
    assert(state == 100);   

    // Exchange
    int32_t old = sqlite_atomic_exchange(&state, (int32_t)50);
    assert(old == 100);
    assert(state == 50);

    // Increment / Decrement
    int32_t val = sqlite_atomic_increment(&state);
    assert(val == 51);

    val = sqlite_atomic_decrement(&state);
    assert(val == 50);

    // Arithmetic
    old = sqlite_atomic_fetch_add(&state, (int32_t)10);
    assert(old == 50);
    assert(state == 60);
}

void test_64bit() {
    int64_t state = 0;
    
    // Store & Load
    sqlite_atomic_store(&state, (int64_t)42);
    assert(sqlite_atomic_load(&state) == 42);

    // CAS Failure
    int64_t expected = 0;
    int success = sqlite_atomic_cas_strong(&state, &expected, (int64_t)100);
    assert(success == 0); 
    assert(expected == 42); 

    // CAS Success
    expected = 42; 
    success = sqlite_atomic_cas_strong(&state, &expected, (int64_t)100);
    assert(success == 1); 
    assert(state == 100);   

    // Exchange
    int64_t old = sqlite_atomic_exchange(&state, (int64_t)50);
    assert(old == 100);
    assert(state == 50);
}

void test_ptr() {
    void* state = nullptr;
    void* a = (void*)1;
    void* b = (void*)2;
    void* c = (void*)3;
    
    // Store & Load
    sqlite_atomic_store(&state, a);
    assert(sqlite_atomic_load(&state) == a);

    // CAS Failure
    void* expected = nullptr;
    int success = sqlite_atomic_cas_strong(&state, &expected, b);
    assert(success == 0);
    assert(expected == a);
    
    // CAS Success
    expected = a;
    success = sqlite_atomic_cas_strong(&state, &expected, b);
    assert(success == 1);
    assert(state == b);

    // Exchange
    void* old = sqlite_atomic_exchange(&state, c);
    assert(old == b);
    assert(state == c);
}

void test_bool() {
    bool state = false;
    
    // Store & Load
    sqlite_atomic_store(&state, true);
    assert(sqlite_atomic_load(&state) == true);

    // CAS
    bool expected = false;
    int success = sqlite_atomic_cas_strong(&state, &expected, true);
    assert(success == 0);
    assert(expected == true);
    
    expected = true;
    success = sqlite_atomic_cas_strong(&state, &expected, false);
    assert(success == 1);
    assert(state == false);
    
    // Exchange
    bool old = sqlite_atomic_exchange(&state, true);
    assert(old == false);
    assert(state == true);
}

int main() {
    test_8bit();
    test_32bit();
    test_64bit();
    test_ptr();
    test_bool();
    
    printf("test_atomic_cpp: PASSED\n");
    return 0;
}
