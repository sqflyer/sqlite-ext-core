#define SQLITE_CORE
#include <stdio.h>
#include <assert.h>
#include "../../include/sqlite3_smart_ptr.hpp"

// Custom placement new since we are building with no-std
inline void* operator new(size_t, void* p) { return p; }


struct MyStruct {
    int data;
    static int delete_count;
    
    MyStruct() : data(0) {}
    ~MyStruct() {
        delete_count++;
    }
    
    void do_something() { data++; }
};

int MyStruct::delete_count = 0;

static int custom_deleter_count = 0;
void my_custom_deleter(MyStruct* ptr) {
    if (ptr) {
        custom_deleter_count++;
        ptr->~MyStruct();
        sqlite3_free(ptr);
    }
}

void test_cpp_shared_ptr() {
    // 1. Default constructor & bool operator
    SqliteSharedPtr<MyStruct> sp_empty;
    assert(!sp_empty);
    assert(sp_empty.use_count() == 0);
    assert(sp_empty.get() == nullptr);

    // 2. Standard construct & access
    MyStruct* raw = (MyStruct*)sqlite3_malloc(sizeof(MyStruct));
    new (raw) MyStruct();
    
    {
        SqliteSharedPtr<MyStruct> sp1(raw);
        assert(sp1);
        assert(sp1.use_count() == 1);
        
        // test operator-> and operator*
        sp1->data = 5;
        assert((*sp1).data == 5);
        sp1->do_something();
        assert(sp1->data == 6);
        
        // 3. Copy/Move
        {
            SqliteSharedPtr<MyStruct> sp2 = sp1; // Copy construct
            assert(sp1.use_count() == 2);
            
            SqliteSharedPtr<MyStruct> sp3;
            sp3 = sp2; // Copy assignment
            assert(sp1.use_count() == 3);
            
            SqliteSharedPtr<MyStruct> sp4 = sqlite_move_ptr(sp3); // Move construct
            assert(sp4.use_count() == 3);
            assert(!sp3);
            
            SqliteSharedPtr<MyStruct> sp5;
            sp5 = sqlite_move_ptr(sp4); // Move assignment
            assert(sp5.use_count() == 3);
            assert(!sp4);
        }
        assert(sp1.use_count() == 1);
        
        // 4. Reset
        sp1.reset();
        assert(!sp1);
        assert(sp1.use_count() == 0);
    }
    assert(MyStruct::delete_count == 1);
    
    // 5. Custom Deleter
    MyStruct* raw2 = (MyStruct*)sqlite3_malloc(sizeof(MyStruct));
    new (raw2) MyStruct();
    {
        SqliteSharedPtr<MyStruct> sp_custom(raw2, my_custom_deleter);
        assert(sp_custom.use_count() == 1);
    }
    assert(custom_deleter_count == 1);
    assert(MyStruct::delete_count == 2);
    
    // Custom Deleter with nullptr
    {
        SqliteSharedPtr<MyStruct> sp_custom_null(nullptr, my_custom_deleter);
        assert(!sp_custom_null);
    }
    
    // 6. Reset with new ptr
    MyStruct* raw3 = (MyStruct*)sqlite3_malloc(sizeof(MyStruct));
    new (raw3) MyStruct();
    {
        SqliteSharedPtr<MyStruct> sp_replace;
        sp_replace.reset(raw3);
        assert(sp_replace.use_count() == 1);
        
        // Reset with nullptr explicitly
        sp_replace.reset(nullptr);
        assert(!sp_replace);
        assert(sp_replace.use_count() == 0);
    }
    assert(MyStruct::delete_count == 3);
    
    // 7. Self Assignment
    MyStruct* raw4 = (MyStruct*)sqlite3_malloc(sizeof(MyStruct));
    new (raw4) MyStruct();
    {
        SqliteSharedPtr<MyStruct> sp_self(raw4);
        sp_self = sp_self; // self copy
        assert(sp_self.use_count() == 1);
        
        sp_self = sqlite_move_ptr(sp_self); // self move
        assert(sp_self.use_count() == 1);
    }
    assert(MyStruct::delete_count == 4);
}

void test_cpp_unique_ptr() {
    // 1. Default construct & bool operator
    SqliteUniquePtr<MyStruct> up_empty;
    assert(!up_empty);
    assert(up_empty.get() == nullptr);
    
    // 2. Standard construct & access
    MyStruct* raw = (MyStruct*)sqlite3_malloc(sizeof(MyStruct));
    new (raw) MyStruct();
    {
        SqliteUniquePtr<MyStruct> up(raw);
        assert(up);
        up->data = 10;
        assert((*up).data == 10);
        
        // 3. Move semantics
        SqliteUniquePtr<MyStruct> up2 = sqlite_move_ptr(up); // Move construct
        assert(!up);
        assert(up2);
        
        SqliteUniquePtr<MyStruct> up3;
        up3 = sqlite_move_ptr(up2); // Move assignment
        assert(!up2);
        assert(up3);
        
        // 4. Release
        MyStruct* released = up3.release();
        assert(!up3);
        
        // 5. Reset
        up3.reset(released);
        assert(up3);
        up3.reset();
        assert(!up3);
    }
    assert(MyStruct::delete_count == 5);
    
    // 6. Custom Deleter
    MyStruct* raw2 = (MyStruct*)sqlite3_malloc(sizeof(MyStruct));
    new (raw2) MyStruct();
    {
        SqliteUniquePtr<MyStruct> up_custom(raw2, my_custom_deleter);
        assert(up_custom);
        
        // Self-move assignment
        up_custom = sqlite_move_ptr(up_custom);
        assert(up_custom);
    }
    assert(custom_deleter_count == 2);
    assert(MyStruct::delete_count == 6);
    
    // Custom Deleter with nullptr
    {
        SqliteUniquePtr<MyStruct> up_custom_null(nullptr, my_custom_deleter);
        assert(!up_custom_null);
    }
}

void test_cpp_weak_ptr() {
    SqliteWeakPtr<MyStruct> wp_empty;
    assert(wp_empty.expired());
    assert(wp_empty.lock().get() == nullptr);
    
    MyStruct* raw = (MyStruct*)sqlite3_malloc(sizeof(MyStruct));
    new (raw) MyStruct();
    
    SqliteSharedPtr<MyStruct> sp(raw);
    {
        // 1. Construct & lock
        SqliteWeakPtr<MyStruct> wp(sp);
        assert(!wp.expired());
        
        SqliteSharedPtr<MyStruct> locked = wp.lock();
        assert(locked.get() == raw);
        assert(locked.use_count() == 2);
        locked.reset();
        
        // 2. Copy/Move
        SqliteWeakPtr<MyStruct> wp2 = wp; // Copy construct
        SqliteWeakPtr<MyStruct> wp3;
        wp3 = wp2; // Copy assignment
        
        SqliteWeakPtr<MyStruct> wp4 = sqlite_move_ptr(wp3); // Move construct
        SqliteWeakPtr<MyStruct> wp5;
        wp5 = sqlite_move_ptr(wp4); // Move assignment
        
        assert(!wp5.expired());
        
        // 3. Reset
        wp5.reset();
        assert(wp5.expired());
        
        // 4. Expiration
        sp.reset();
        assert(wp.expired());
        assert(wp.lock().get() == nullptr);
    }
    assert(MyStruct::delete_count == 7);
    
    // 5. Self assignment
    {
        SqliteWeakPtr<MyStruct> wp_self;
        wp_self = wp_self;
        wp_self = sqlite_move_ptr(wp_self);
        assert(wp_self.expired());
        
        MyStruct* raw_self = (MyStruct*)sqlite3_malloc(sizeof(MyStruct));
        new (raw_self) MyStruct();
        SqliteSharedPtr<MyStruct> sp_self(raw_self);
        SqliteWeakPtr<MyStruct> wp_valid(sp_self);
        
        wp_valid = wp_valid; // self copy valid
        assert(!wp_valid.expired());
        
        wp_valid = sqlite_move_ptr(wp_valid); // self move valid
        assert(!wp_valid.expired());
    }
    assert(MyStruct::delete_count == 8);
}

int main() {
    sqlite3_initialize();
    
    test_cpp_shared_ptr();
    printf("C++ Shared Pointer Tests Passed!\n");
    
    test_cpp_unique_ptr();
    printf("C++ Unique Pointer Tests Passed!\n");
    
    test_cpp_weak_ptr();
    printf("C++ Weak Pointer Tests Passed!\n");
    
    sqlite3_shutdown();
    return 0;
}
