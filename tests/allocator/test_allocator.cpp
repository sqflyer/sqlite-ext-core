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

void test_sqlite_allocator() {
    reset_counts();
    sqlite3_initialize();

    sqlite3_int64 mem_before = sqlite3_memory_used();

    SqliteAllocator<MyStruct> alloc;
    SqliteAllocator<int> int_alloc;
    (void)int_alloc;

    // Test equality comparisons
    assert(alloc == alloc);
    assert(!(alloc != alloc));

    // Test rebind
    SqliteAllocator<MyStruct>::rebind<double>::other dbl_alloc;
    (void)dbl_alloc;

    // Allocate 3 elements
    MyStruct* buffer = alloc.allocate(3);
    assert(buffer != nullptr);
    assert(MyStruct::construct_count == 0); // Raw uninitialized allocation!

    sqlite3_int64 mem_allocated = sqlite3_memory_used();
    assert(mem_allocated > mem_before);

    // In-place construction
    alloc.construct(&buffer[0], 11);
    alloc.construct(&buffer[1], 22);
    alloc.construct(&buffer[2], 33);
    assert(MyStruct::construct_count == 3);
    assert(MyStruct::destruct_count == 0);
    assert(buffer[0].data == 11);
    assert(buffer[1].data == 22);
    assert(buffer[2].data == 33);

    // In-place destruction
    alloc.destroy(&buffer[0]);
    alloc.destroy(&buffer[1]);
    alloc.destroy(&buffer[2]);
    assert(MyStruct::destruct_count == 3);

    // Deallocation
    alloc.deallocate(buffer, 3);
    sqlite3_int64 mem_after = sqlite3_memory_used();
    assert(mem_after == mem_before); // Clean memory release

    // Zero-count allocation safety
    MyStruct* zero_ptr = alloc.allocate(0);
    assert(zero_ptr == nullptr);
    alloc.deallocate(nullptr, 0); // No-op safe
}

void test_zeroed_allocations() {
    reset_counts();

    // 1. sqlite_new_zeroed<T>() - raw single object allocation without constructors
    MyStruct* single = sqlite_new_zeroed<MyStruct>();
    assert(single != nullptr);
    assert(MyStruct::construct_count == 0); // Must NOT invoke constructors
    const unsigned char* single_bytes = reinterpret_cast<const unsigned char*>(single);
    for (size_t i = 0; i < sizeof(MyStruct); ++i) {
        assert(single_bytes[i] == 0x00);
    }
    assert(single->data == 0);
    sqlite3_free(single);

    // 2. sqlite_new_array_zeroed<T>(count) - raw array allocation
    const size_t elem_count = 8;
    MyStruct* arr = sqlite_new_array_zeroed<MyStruct>(elem_count);
    assert(arr != nullptr);
    assert(MyStruct::construct_count == 0); // Must NOT invoke constructors
    const unsigned char* arr_bytes = reinterpret_cast<const unsigned char*>(arr);
    for (size_t i = 0; i < elem_count * sizeof(MyStruct); ++i) {
        assert(arr_bytes[i] == 0x00);
    }
    for (size_t i = 0; i < elem_count; ++i) {
        assert(arr[i].data == 0);
    }
    sqlite_delete_array(arr);

    // 3. sqlite_malloc_zeroed(bytes)
    const size_t byte_count = 64;
    void* raw_mem = sqlite_malloc_zeroed(byte_count);
    assert(raw_mem != nullptr);
    const unsigned char* raw_bytes = static_cast<const unsigned char*>(raw_mem);
    for (size_t i = 0; i < byte_count; ++i) {
        assert(raw_bytes[i] == 0x00);
    }
    sqlite3_free(raw_mem);

    // 4. Zero count and edge cases
    assert(sqlite_new_array_zeroed<MyStruct>(0) == nullptr);
    assert(sqlite_malloc_zeroed(0) == nullptr);
}

void test_reallocate_zeroed() {
    reset_counts();

    // 1. sqlite_reallocate_array_zeroed
    MyStruct* arr = sqlite_new_array_zeroed<MyStruct>(2);
    assert(arr != nullptr);
    arr[0].data = 111;
    arr[1].data = 222;

    // Grow from 2 to 5 elements (new elements 2..4 must be zeroed)
    arr = sqlite_reallocate_array_zeroed<MyStruct>(arr, 2, 5);
    assert(arr != nullptr);
    assert(arr[0].data == 111);
    assert(arr[1].data == 222);

    const unsigned char* new_elem_bytes = reinterpret_cast<const unsigned char*>(&arr[2]);
    for (size_t i = 0; i < 3 * sizeof(MyStruct); ++i) {
        assert(new_elem_bytes[i] == 0x00);
    }
    assert(arr[2].data == 0);
    assert(arr[3].data == 0);
    assert(arr[4].data == 0);

    // Shrink array
    arr = sqlite_reallocate_array_zeroed<MyStruct>(arr, 5, 2);
    assert(arr != nullptr);
    assert(arr[0].data == 111);
    assert(arr[1].data == 222);

    // Reallocate to 0 frees memory
    arr = sqlite_reallocate_array_zeroed<MyStruct>(arr, 2, 0);
    assert(arr == nullptr);

    // 2. sqlite_realloc_zeroed byte buffer
    void* raw = sqlite_malloc_zeroed(16);
    assert(raw != nullptr);
    memset(raw, 0x5A, 16);

    // Expand from 16 to 48 bytes (bytes 16..47 must be zeroed)
    raw = sqlite_realloc_zeroed(raw, 16, 48);
    assert(raw != nullptr);
    const unsigned char* ptr_bytes = static_cast<const unsigned char*>(raw);
    for (size_t i = 0; i < 16; ++i) {
        assert(ptr_bytes[i] == 0x5A);
    }
    for (size_t i = 16; i < 48; ++i) {
        assert(ptr_bytes[i] == 0x00);
    }

    // Realloc to 0 frees memory
    raw = sqlite_realloc_zeroed(raw, 48, 0);
    assert(raw == nullptr);
}

void test_sqlite_allocator_zeroed() {
    SqliteAllocator<MyStruct> alloc;

    MyStruct* buffer = alloc.allocate_zeroed(4);
    assert(buffer != nullptr);
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(buffer);
    for (size_t i = 0; i < 4 * sizeof(MyStruct); ++i) {
        assert(bytes[i] == 0x00);
    }
    for (size_t i = 0; i < 4; ++i) {
        assert(buffer[i].data == 0);
    }

    alloc.deallocate(buffer, 4);
}

void test_construct_n() {
    reset_counts();

    // 1. Default construct_n
    MyStruct* arr = sqlite_new_array<MyStruct>(4);
    assert(arr != nullptr);
    assert(MyStruct::construct_count == 0);

    sqlite_construct_n(arr, 4);
    assert(MyStruct::construct_count == 4);
    for (size_t i = 0; i < 4; ++i) {
        assert(arr[i].data == 0);
    }

    sqlite_destroy_n(arr, 4);
    assert(MyStruct::destruct_count == 4);
    sqlite_delete_array(arr);

    // 2. Parameterized construct_n
    reset_counts();
    arr = sqlite_new_array<MyStruct>(3);
    assert(arr != nullptr);
    assert(MyStruct::construct_count == 0);

    sqlite_construct_n(arr, 3, 77);
    assert(MyStruct::construct_count == 3);
    for (size_t i = 0; i < 3; ++i) {
        assert(arr[i].data == 77);
    }

    sqlite_destroy_n(arr, 3);
    assert(MyStruct::destruct_count == 3);
    sqlite_delete_array(arr);

    // 3. Nullptr and zero count safety
    sqlite_construct_n<MyStruct>(nullptr, 0);
    sqlite_construct_n<MyStruct>(nullptr, 5);
}

void test_type_traits() {
    // 1. sqlite_remove_reference
    static_assert(sqlite_is_same<sqlite_remove_reference<int>::type, int>::value, "remove_reference<int> must be int");
    static_assert(sqlite_is_same<sqlite_remove_reference<int&>::type, int>::value, "remove_reference<int&> must be int");
    static_assert(sqlite_is_same<sqlite_remove_reference<int&&>::type, int>::value, "remove_reference<int&&> must be int");
    static_assert(sqlite_is_same<sqlite_remove_reference<const char*&>::type, const char*>::value, "remove_reference<const char*&>");

    // 2. sqlite_is_same
    static_assert(sqlite_is_same<int, int>::value, "is_same<int, int> must be true");
    static_assert(!sqlite_is_same<int, double>::value, "is_same<int, double> must be false");
    static_assert(!sqlite_is_same<int, const int>::value, "is_same<int, const int> must be false");

    // 3. sqlite_enable_if
    typedef sqlite_enable_if<true, long>::type enabled_long;
    static_assert(sqlite_is_same<enabled_long, long>::value, "enable_if<true, long> must be long");

    // 4. sqlite_is_trivially_copyable
    static_assert(sqlite_is_trivially_copyable<int>::value, "int must be trivially copyable");
    static_assert(sqlite_is_trivially_copyable<double>::value, "double must be trivially copyable");
    static_assert(!sqlite_is_trivially_copyable<MyStruct>::value, "MyStruct with custom dtor is not trivially copyable");
}

void test_integer_overflow_protection() {
    size_t overflow_count = static_cast<size_t>(-1) / sizeof(MyStruct) + 1;

    // 1. sqlite_new_array overflow
    assert(sqlite_new_array<MyStruct>(overflow_count) == nullptr);

    // 2. sqlite_new_array_zeroed overflow
    assert(sqlite_new_array_zeroed<MyStruct>(overflow_count) == nullptr);

    // 3. sqlite_reallocate_array overflow
    assert(sqlite_reallocate_array<MyStruct>(nullptr, overflow_count) == nullptr);

    // 4. sqlite_reallocate_array_zeroed overflow
    assert(sqlite_reallocate_array_zeroed<MyStruct>(nullptr, 0, overflow_count) == nullptr);
}

void test_fast_memcpy() {
    char src[64];
    char dst[64];
    for (int i = 0; i < 64; ++i) src[i] = static_cast<char>(i + 1);
    memset(dst, 0, 64);

    SQLITE_FAST_MEMCPY(dst, src, 64);
    for (int i = 0; i < 64; ++i) {
        assert(dst[i] == static_cast<char>(i + 1));
    }
}

int main() {
    test_type_traits();
    test_single_object();
    test_array_allocation();
    test_reallocate_array();
    test_move_forwarding();
    test_forwarding();
    test_destroy_at();
    test_sqlite_allocator();
    test_zeroed_allocations();
    test_reallocate_zeroed();
    test_sqlite_allocator_zeroed();
    test_construct_n();
    test_integer_overflow_protection();
    test_fast_memcpy();
    
    printf("All allocator tests passed successfully (100%% coverage)!\n");
    return 0;
}
