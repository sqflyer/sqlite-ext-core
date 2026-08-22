#define SQLITE_CORE
#include <stdio.h>
#include <assert.h>
#include "../../include/sqlite3_smart_ptr.h"

typedef struct {
    int data;
    int freed;
} MyStruct;

static MyStruct* g_last_freed = 0;

void my_free(MyStruct* ptr) {
    if (ptr) {
        ptr->freed = 1;
        g_last_freed = ptr;
        sqlite3_free(ptr);
    }
}

SQLITE_SHARED_PTR_DEFINE(MyStruct, MyStruct, my_free)
SQLITE_UNIQUE_PTR_DEFINE(MyStruct, MyStruct, my_free)

int main() {
    sqlite3_initialize();
    
    MyStruct* raw = (MyStruct*)sqlite3_malloc(sizeof(MyStruct));
    raw->data = 42;
    raw->freed = 0;
    
    MyStruct_SharedPtr sp1 = MyStruct_make_shared(raw);
    assert(sp1.cb != 0);
    assert(sp1.cb->strong_count == 1);
    assert(MyStruct_get(sp1) == raw);
    
    MyStruct_SharedPtr sp2 = MyStruct_clone(sp1);
    assert(sp1.cb->strong_count == 2);
    
    // Test C SharedPtr Move
    MyStruct_SharedPtr sp_moved = MyStruct_move(&sp1);
    assert(sp1.cb == 0); // Original is zeroed out
    assert(sp_moved.cb != 0);
    assert(sp_moved.cb->strong_count == 2); // Ref count didn't change
    
    MyStruct_release(&sp_moved);
    assert(sp2.cb->strong_count == 1);
    
    MyStruct_release(&sp2);
    assert(g_last_freed == raw);
    
    // Test C SharedPtr Reset
    MyStruct* raw3 = (MyStruct*)sqlite3_malloc(sizeof(MyStruct));
    raw3->data = 33;
    MyStruct_SharedPtr sp3 = MyStruct_make_shared(raw3);
    MyStruct_reset(&sp3);
    assert(g_last_freed == raw3);
    assert(sp3.cb == 0);
    
    // Test C SharedPtr Null & Edge Cases
    MyStruct_SharedPtr sp_null = MyStruct_make_shared(0);
    assert(sp_null.cb == 0);
    assert(MyStruct_get(sp_null) == 0);
    
    MyStruct_SharedPtr sp_null2 = MyStruct_clone(sp_null);
    assert(sp_null2.cb == 0);
    
    MyStruct_SharedPtr sp_null_moved = MyStruct_move(&sp_null2);
    assert(sp_null_moved.cb == 0);
    assert(sp_null2.cb == 0);
    
    MyStruct_SharedPtr sp_null_moved_zero = MyStruct_move(0);
    assert(sp_null_moved_zero.cb == 0);
    
    MyStruct_release(&sp_null); // should not crash
    MyStruct_reset(&sp_null_moved); // should not crash
    MyStruct_release(0); // should not crash
    
    printf("C Shared Pointer Tests Passed!\n");
    
    MyStruct* raw2 = (MyStruct*)sqlite3_malloc(sizeof(MyStruct));
    raw2->data = 100;
    raw2->freed = 0;
    
    MyStruct_UniquePtr up1 = MyStruct_make_unique(raw2);
    assert(MyStruct_get_unique(up1) == raw2);
    
    // Test C UniquePtr Move
    MyStruct_UniquePtr up_moved = MyStruct_move_unique(&up1);
    assert(MyStruct_get_unique(up1) == 0); // Original is zeroed out
    assert(MyStruct_get_unique(up_moved) == raw2);
    
    MyStruct_UniquePtr up_moved_zero = MyStruct_move_unique(0);
    assert(MyStruct_get_unique(up_moved_zero) == 0);
    
    MyStruct_release_unique(&up_moved);
    assert(g_last_freed == raw2);
    assert(MyStruct_get_unique(up_moved) == 0);
    
    // Test C UniquePtr Null
    MyStruct_UniquePtr up_null = MyStruct_make_unique(0);
    assert(MyStruct_get_unique(up_null) == 0);
    MyStruct_release_unique(&up_null); // should not crash
    MyStruct_release_unique(0); // should not crash
    
    printf("C Unique Pointer Tests Passed!\n");
    
    // Test C WeakPtr
    MyStruct* raw4 = (MyStruct*)sqlite3_malloc(sizeof(MyStruct));
    raw4->data = 44;
    MyStruct_SharedPtr sp4 = MyStruct_make_shared(raw4);
    
    MyStruct_WeakPtr wp1 = MyStruct_weak_create(sp4);
    assert(wp1.cb != 0);
    assert(!MyStruct_weak_expired(wp1));
    
    MyStruct_WeakPtr wp2 = MyStruct_weak_clone(wp1);
    assert(wp2.cb->weak_count == 3);
    
    MyStruct_WeakPtr wp3 = MyStruct_weak_move(&wp2);
    assert(wp2.cb == 0);
    assert(wp3.cb != 0);
    assert(wp3.cb->weak_count == 3);
    
    MyStruct_SharedPtr sp5 = MyStruct_weak_lock(wp3);
    assert(sp5.cb != 0);
    assert(sp5.cb->strong_count == 2);
    MyStruct_release(&sp5);
    
    MyStruct_release(&sp4);
    assert(MyStruct_weak_expired(wp1));
    assert(MyStruct_weak_expired(wp3));
    
    MyStruct_SharedPtr sp6 = MyStruct_weak_lock(wp1);
    assert(sp6.cb == 0); // Cannot lock expired weak ptr
    
    MyStruct_weak_release(&wp1);
    MyStruct_weak_release(&wp3);
    
    // Test C WeakPtr Edge Cases
    MyStruct_SharedPtr sp_empty = MyStruct_make_shared(0);
    MyStruct_WeakPtr wp_null = MyStruct_weak_create(sp_empty);
    assert(wp_null.cb == 0);
    assert(MyStruct_weak_expired(wp_null));
    
    MyStruct_WeakPtr wp_null2 = MyStruct_weak_clone(wp_null);
    assert(wp_null2.cb == 0);
    
    MyStruct_WeakPtr wp_null_moved = MyStruct_weak_move(&wp_null2);
    assert(wp_null_moved.cb == 0);
    
    MyStruct_WeakPtr wp_null_moved_zero = MyStruct_weak_move(0);
    assert(wp_null_moved_zero.cb == 0);
    
    MyStruct_weak_reset(&wp_null_moved); // shouldn't crash
    MyStruct_weak_release(0); // shouldn't crash
    
    MyStruct* raw5 = (MyStruct*)sqlite3_malloc(sizeof(MyStruct));
    raw5->data = 55;
    MyStruct_SharedPtr sp5_valid = MyStruct_make_shared(raw5);
    MyStruct_WeakPtr wp_valid = MyStruct_weak_create(sp5_valid);
    wp_valid = MyStruct_weak_move(&wp_valid); // self move
    assert(wp_valid.cb != 0);
    MyStruct_weak_release(&wp_valid);
    MyStruct_release(&sp5_valid);
    
    printf("C Weak Pointer Tests Passed!\n");
    
    sqlite3_shutdown();
    return 0;
}
