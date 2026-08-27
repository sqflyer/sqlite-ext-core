#define SQLITE_CORE
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "../../include/sqlite3_allocator.hpp"

struct MyStruct {
    int data;
    static int construct_count;
    static int destruct_count;
    
    MyStruct() : data(0) { construct_count++; }
    MyStruct(int d) : data(d) { construct_count++; }
    
    // Copy constructor
    MyStruct(const MyStruct& other) : data(other.data) { construct_count++; }
    
    // Move constructor
    MyStruct(MyStruct&& other) noexcept : data(other.data) {
        other.data = -1;
        construct_count++;
    }

    ~MyStruct() {
        destruct_count++;
    }
};

int MyStruct::construct_count = 0;
int MyStruct::destruct_count = 0;

void reset_counts() {
    MyStruct::construct_count = 0;
    MyStruct::destruct_count = 0;
}

void test_single_object() {
    reset_counts();
    
    MyStruct* obj = sqlite_new<MyStruct>(42);
    assert(obj != nullptr);
    assert(obj->data == 42);
    assert(MyStruct::construct_count == 1);
    assert(MyStruct::destruct_count == 0);
    
    sqlite_delete(obj);
    assert(MyStruct::destruct_count == 1);
}

void test_array_allocation() {
    reset_counts();
    
    // Allocate raw array (no constructors)
    MyStruct* arr = sqlite_new_array<MyStruct>(3);
    assert(arr != nullptr);
    assert(MyStruct::construct_count == 0); // Uninitialized!
    
    // Manually construct
    for (size_t i = 0; i < 3; ++i) {
        sqlite_construct_at(&arr[i], (int)(i + 1) * 10);
    }
    assert(MyStruct::construct_count == 3);
    assert(arr[0].data == 10);
    assert(arr[1].data == 20);
    assert(arr[2].data == 30);
    
    // Manually destruct
    sqlite_destroy_n(arr, 3);
    assert(MyStruct::destruct_count == 3);
    
    // Free raw memory
    sqlite_delete_array(arr);
}

void test_move_forwarding() {
    reset_counts();
    
    MyStruct original(100);
    assert(MyStruct::construct_count == 1);
    
    // Test move via perfect forwarding
    MyStruct* moved_obj = sqlite_new<MyStruct>(sqlite_move_ptr(original));
    assert(moved_obj->data == 100);
    assert(original.data == -1); // Moved from!
    assert(MyStruct::construct_count == 2);
    
    sqlite_delete(moved_obj);
}

void test_forwarding() {
    reset_counts();
    
    MyStruct original(200);
    assert(MyStruct::construct_count == 1);
    
    // Explicitly test sqlite_forward with an lvalue (should copy)
    MyStruct* copied_obj = sqlite_new<MyStruct>(sqlite_forward<MyStruct&>(original));
    assert(copied_obj->data == 200);
    assert(original.data == 200); // Not moved!
    assert(MyStruct::construct_count == 2);
    
    sqlite_delete(copied_obj);
}

void test_destroy_at() {
    reset_counts();
    
    // Allocate raw memory
    MyStruct* raw = (MyStruct*)sqlite3_malloc(sizeof(MyStruct));
    
    // Manually construct
    sqlite_construct_at(raw, 55);
    assert(MyStruct::construct_count == 1);
    assert(MyStruct::destruct_count == 0);
    
    // Explicitly test sqlite_destroy_at
    sqlite_destroy_at(raw);
    assert(MyStruct::destruct_count == 1);
    
    // Free raw memory
    sqlite3_free(raw);
}

void test_reallocate_array() {
    reset_counts();

    // 1. Initial allocation: size 2
    MyStruct* arr = sqlite_new_array<MyStruct>(2);
    assert(arr != nullptr);

    sqlite_construct_at(&arr[0], 100);
    sqlite_construct_at(&arr[1], 200);
    assert(MyStruct::construct_count == 2);

    // 2. Grow array to size 4
    arr = sqlite_reallocate_array<MyStruct>(arr, 4);
    assert(arr != nullptr);
    assert(arr[0].data == 100);
    assert(arr[1].data == 200);

    sqlite_construct_at(&arr[2], 300);
    sqlite_construct_at(&arr[3], 400);
    assert(MyStruct::construct_count == 4);

    // 3. Shrink array to size 3 (destroy trimmed element first)
    sqlite_destroy_at(&arr[3]);
    assert(MyStruct::destruct_count == 1);

    arr = sqlite_reallocate_array<MyStruct>(arr, 3);
    assert(arr != nullptr);
    assert(arr[0].data == 100);
    assert(arr[1].data == 200);
    assert(arr[2].data == 300);

    // 4. Clean up remaining elements
    sqlite_destroy_n(arr, 3);
    assert(MyStruct::destruct_count == 4);

    // 5. Reallocate to 0 (should free memory and return nullptr)
    arr = sqlite_reallocate_array<MyStruct>(arr, 0);
    assert(arr == nullptr);

    // 6. Reallocating nullptr should act as new allocation
    arr = sqlite_reallocate_array<MyStruct>(nullptr, 2);
    assert(arr != nullptr);
    sqlite_delete_array(arr);
}

int main() {
    test_single_object();
    test_array_allocation();
    test_reallocate_array();
    test_move_forwarding();
    test_forwarding();
    test_destroy_at();
    
    printf("All allocator tests passed successfully!\n");
    return 0;
}
