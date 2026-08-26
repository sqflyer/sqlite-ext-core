#define SQLITE_CORE
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "sqlite3_buffer.hpp"

void test_buffer() {
    printf("1. Testing SqliteBuffer dynamic expansion...\n");
    
    SqliteBuffer buf;
    assert(buf.bytes() == 0);
    assert(buf.capacity() == 0);
    
    const char* chunk1 = "Hello";
    buf.append(chunk1, 5);
    assert(buf.bytes() == 5);
    assert(buf.capacity() >= 5);
    
    const char* chunk2 = " World";
    buf.append(chunk2, 6);
    assert(buf.bytes() == 11);
    
    // Test geometric growth (capacity * 2) vs exact required
    buf.append("12345678901234567890", 20); // forces capacity up
    assert(buf.capacity() >= 31);
    
    // Test nullptr append (should safely no-op and return true)
    assert(buf.append(nullptr, 100) == true);
    assert(buf.append("dummy", 0) == true);
    
    // Test clear
    sqlite3_int64 cap_before = buf.capacity();
    buf.clear();
    assert(buf.bytes() == 0);
    assert(buf.capacity() == cap_before); // Should not shrink
    
    // Test move constructor
    SqliteBuffer buf2(sqlite_move(buf));
    assert(buf2.capacity() == cap_before);
    assert(buf.capacity() == 0);
    
    // Test move assignment
    SqliteBuffer buf3;
    buf3 = sqlite_move(buf2);
    assert(buf3.capacity() == cap_before);
    assert(buf2.capacity() == 0);
    
    // Test self-assignment (no-op)
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-move"
#endif
    buf3 = sqlite_move(buf3);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    assert(buf3.capacity() == cap_before);
    
    // Test equality
    SqliteBuffer eq_buf1;
    SqliteBuffer eq_buf2;
    assert(eq_buf1 == eq_buf2);
    
    eq_buf1.append("test", 4);
    assert(eq_buf1 != eq_buf2);
    assert(eq_buf1 > eq_buf2);
    assert(eq_buf1 >= eq_buf2);
    assert(eq_buf2 < eq_buf1);
    assert(eq_buf2 <= eq_buf1);
    
    eq_buf2.append("test", 4);
    assert(eq_buf1 == eq_buf2);
    assert(eq_buf1 <= eq_buf2);
    assert(eq_buf1 >= eq_buf2);
    
    // Lexicographical check
    SqliteBuffer eq_buf3;
    eq_buf3.append("teta", 4); // 't' > 's', so "teta" > "test"
    assert(eq_buf1 < eq_buf3);
    
    // Hash check
    SqliteBuffer empty_buf;
    assert(eq_buf1.hash() == eq_buf2.hash());
    assert(eq_buf1.hash() != eq_buf3.hash());
    assert(empty_buf.hash() != eq_buf1.hash());
    
    // Test equality with SqliteValue (mocking SQLITE_NULL)
    sqlite3_value* mock_val2 = nullptr;
    SqliteValueView view2(mock_val2);
    assert(eq_buf1 != view2);
    assert(view2 != eq_buf1);
    
    // A null value is not equal to an empty buffer (types differ)
    assert(empty_buf != view2);
    assert(view2 != empty_buf);
    
    // Hash stability check post-clear
    unsigned long long hash_before = eq_buf1.hash();
    eq_buf1.clear();
    assert(eq_buf1.hash() != hash_before);
    assert(eq_buf1.hash() == empty_buf.hash()); // empty hashes match
    
    // Hash stability check post-move
    SqliteBuffer moving_buf;
    moving_buf.append("move", 4);
    unsigned long long expected_move_hash = moving_buf.hash();
    SqliteBuffer moved_buf = std::move(moving_buf);
    assert(moved_buf.hash() == expected_move_hash);
    assert(moving_buf.hash() == empty_buf.hash()); // Moved-from is empty
    
    // Slice test
    SqliteBuffer slice_target;
    slice_target.append("HelloWorld", 10);
    
    SqliteBufferSlice slice1 = slice_target.bufferSlice(0, 5);
    assert(slice1.bytes() == 5);
    assert(memcmp(slice1.data(), "Hello", 5) == 0);
    
    SqliteBufferSlice slice2 = slice_target.bufferSlice(5, 100); // Out of bounds length
    assert(slice2.bytes() == 5);
    assert(memcmp(slice2.data(), "World", 5) == 0);
    
    SqliteBufferSlice slice3 = slice_target.bufferSlice(20, 5); // Invalid offset
    assert(slice3.bytes() == 0);
    
    // Test Slice Comparisons
    assert(slice1 == "Hello");
    assert(slice1 != "World");
    assert(slice1 < SqliteBufferSlice("World", 5)); // 'H' < 'W'
    assert(slice2 > SqliteBufferSlice("Hello", 5)); // 'W' > 'H'
    
    // Test Slice vs Buffer Comparisons
    SqliteBuffer target_clone;
    target_clone.append("Hello", 5);
    assert(slice1 == target_clone); // Slice == Buffer
    assert(target_clone == slice1); // Buffer == Slice
    assert(slice2 != target_clone);
    
    // Test Slice Hashing matches Buffer Hashing
    assert(slice1.hash() == target_clone.hash());
    
    // Test Nullptr comparisons
    SqliteBufferSlice empty_slice;
    assert(empty_slice == nullptr);
    assert(slice1 != nullptr);
}

void test_string() {
    printf("2. Testing SqliteString std::string emulation...\n");
    
    SqliteString str("Hello");
    assert(str.length() == 5);
    assert(str == "Hello");
    
    str.append(" World");
    assert(str.length() == 11);
    assert(str == "Hello World");
    assert(str != "Hello");
    
    // Test implicit null termination
    const char* c = str.c_str();
    assert(c[11] == '\0');
    
    // Test nullptr handling
    assert(str.append(nullptr) == true);
    assert(str == "Hello World");
    assert(str >= "Hello World");
    assert(str <= "Hello World");
    
    assert(str > "Hello");
    assert(str < "Hello World!");
    
    // Test equality with nullptr
    assert(str != nullptr);
    assert(str > nullptr); // string is greater than null
    
    SqliteString empty_str;
    assert(empty_str == nullptr);
    assert(nullptr == empty_str);
    assert(empty_str <= nullptr);
    
    // Hash check and stability
    assert(str.hash() != empty_str.hash());
    SqliteString hash_str("Hello World");
    assert(str.hash() == hash_str.hash());
    
    unsigned long long text_hash_before = str.hash();
    str.clear();
    assert(str.hash() != text_hash_before);
    assert(str.hash() == empty_str.hash()); // cleared string hashes to empty string
    
    // Test capacity stability on string clear
    sqlite3_int64 cap_str = hash_str.capacity();
    hash_str.clear();
    assert(hash_str.capacity() == cap_str); // capacity is preserved
    assert(hash_str.c_str()[0] == '\0'); // null terminator remains
    
    // Test Move semantics hash preservation
    SqliteString mov_str("moving string");
    unsigned long long mov_hash = mov_str.hash();
    SqliteString moved_str = sqlite_move(mov_str);
    assert(moved_str.hash() == mov_hash);
    assert(mov_str.hash() == empty_str.hash()); // Moved-from is empty
    assert(mov_str == nullptr); // Moved-from safely acts as null
    
    // Test equality with SqliteValue (mocking SQLITE_NULL)
    sqlite3_value* mock_val = nullptr;
    SqliteValueView view(mock_val);
    assert(str != view);
    assert(view != str);
    
    // A null value is not equal to an empty string (types differ)
    assert(empty_str != view);
    assert(view != empty_str);
    
    str.clear();
    assert(str.length() == 0);
    assert(str == "");
    assert(str.c_str()[0] == '\0'); // Verify null term persists after clear
    
    // Test move constructor
    str.append("Moved");
    SqliteString str2(sqlite_move(str));
    assert(str2 == "Moved");
    assert(str.length() == 0);
    
    // Test move assignment
    SqliteString str3;
    str3 = sqlite_move(str2);
    assert(str3 == "Moved");
    assert(str2.length() == 0);
    
    // Test self-assignment
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-move"
#endif
    str3 = sqlite_move(str3);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    assert(str3 == "Moved");
}

int main() {
    // sqlite3_malloc requires initialization
    sqlite3_initialize();

    test_buffer();
    test_string();

    sqlite3_shutdown();

    printf("\nAll 2 SqliteBuffer/SqliteString Test Suites Passed Successfully!\n");
    return 0;
}
