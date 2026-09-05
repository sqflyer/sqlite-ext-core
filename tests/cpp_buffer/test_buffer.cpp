#define SQLITE_CORE
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "sqlite3_buffer.hpp"
#include "sqlite3_value.hpp"

void test_buffer() {
    printf("1. Testing SqliteBuffer dynamic expansion...\n");
    
    SqliteBuffer buf;
    assert(buf.bytes() == 0);
    assert(buf.capacity() == 22);
    assert(buf.is_sbo());
    assert(buf.is_stack());
    assert(!buf.is_heap());
    
    const char* chunk1 = "Hello";
    buf.append(chunk1, 5);
    assert(buf.bytes() == 5);
    assert(buf.capacity() == 22);
    assert(buf.is_sbo());
    
    const char* chunk2 = " World";
    buf.append(chunk2, 6);
    assert(buf.bytes() == 11);
    assert(buf.is_sbo());
    
    // Test geometric growth (capacity * 2) vs exact required
    buf.append("12345678901234567890", 20); // forces capacity up (31 bytes total -> heap)
    assert(buf.capacity() >= 31);
    assert(buf.is_heap());
    assert(!buf.is_sbo());
    
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
    assert(buf.capacity() == 22);
    assert(buf.is_sbo());
    
    // Test move assignment
    SqliteBuffer buf3;
    buf3 = sqlite_move(buf2);
    assert(buf3.capacity() == cap_before);
    assert(buf2.capacity() == 22);
    assert(buf2.is_sbo());
    
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
    SqliteBuffer moved_buf = sqlite_move(moving_buf);
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

    // Test 22-char SBO behavior & stack/heap discriminator
    SqliteString sbo_empty;
    assert(sbo_empty.is_stack());
    assert(!sbo_empty.is_heap());
    assert(sbo_empty.capacity() == 22);
    assert(sbo_empty.length() == 0);

    // Exact 22 chars (SBO boundary limit with '\0' in 23-byte m_sbo array)
    const char* exact_22 = "1234567890123456789012";
    SqliteString sbo_22(exact_22);
    assert(sbo_22.length() == 22);
    assert(sbo_22.is_stack());
    assert(!sbo_22.is_heap());
    assert(sbo_22.capacity() == 22);
    assert(sbo_22 == exact_22);
    assert(sbo_22.c_str()[22] == '\0');

    // 23 chars (triggers heap transition)
    const char* exact_23 = "12345678901234567890123";
    SqliteString heap_23(exact_23);
    assert(heap_23.length() == 23);
    assert(heap_23.is_heap());
    assert(!heap_23.is_stack());
    assert(heap_23.capacity() >= 23);
    assert(heap_23 == exact_23);

    // Transition from stack to heap via append()
    SqliteString grow_sbo("12345678901234567890"); // 20 chars
    assert(grow_sbo.is_stack());
    grow_sbo.append("12"); // 22 chars total
    assert(grow_sbo.length() == 22);
    assert(grow_sbo.is_stack());
    assert(!grow_sbo.is_heap());

    grow_sbo.append("3"); // 23 chars total -> transitions to heap!
    assert(grow_sbo.length() == 23);
    assert(grow_sbo.is_heap());
    assert(!grow_sbo.is_stack());
    assert(grow_sbo == exact_23);

    // Test take()
    SqliteString taken_str = grow_sbo.take();
    assert(taken_str.is_heap());
    assert(taken_str == exact_23);
    assert(grow_sbo.is_stack());
    assert(grow_sbo.length() == 0);
    assert(grow_sbo == nullptr);
}

void test_try_methods() {
    printf("3. Testing SqliteBuffer and SqliteString try_* methods...\n");
    
    SqliteBuffer buf;
    SqliteStatus st = buf.try_reserve(64);
    assert(st.is_ok());
    assert(buf.capacity() >= 64);
    
    st = buf.try_append("Hello World", 11);
    assert(st.is_ok());
    assert(buf.bytes() == 11);
    assert(memcmp(buf.data(), "Hello World", 11) == 0);
    
    SqliteResult<void*> res_uninit = buf.try_append_uninitialized(5);
    assert(res_uninit.is_ok());
    assert(res_uninit.unwrap() != nullptr);
    memcpy(res_uninit.unwrap(), "12345", 5);
    assert(buf.bytes() == 16);
    
    // SqliteString try_ methods
    SqliteResult<SqliteString> res_str = SqliteString::try_create("Initial Value");
    assert(res_str.is_ok());
    SqliteString str = sqlite_move(res_str.unwrap());
    assert(str == "Initial Value");
    
    st = str.try_append(" - Appended");
    assert(st.is_ok());
    assert(str == "Initial Value - Appended");
    
    SqliteResult<SqliteString> empty_res = SqliteString::try_create(nullptr);
    assert(empty_res.is_ok());
    assert(empty_res.unwrap().length() == 0);
}

void test_sbo_null_termination_and_transitions() {
    printf("4. Testing SBO boundary transitions, null-termination invariants, and reset()...\n");

    // 1. Invariant on empty buffer
    SqliteBuffer buf;
    assert(buf.bytes() == 0);
    assert(buf.capacity() == 22);
    assert(buf.is_sbo());
    assert(buf.c_str() != nullptr);
    assert(buf.c_str()[0] == '\0');

    // 2. Character-by-character append up to 22 bytes (all must stay in SBO)
    for (int i = 0; i < 22; ++i) {
        char ch = static_cast<char>('a' + (i % 26));
        buf.append(&ch, 1);
        assert(buf.bytes() == i + 1);
        assert(buf.is_sbo());
        assert(!buf.is_heap());
        assert(buf.capacity() == 22);
        assert(buf.c_str()[buf.bytes()] == '\0');
    }

    // 3. Appending 23rd byte triggers transition to heap
    char ch23 = 'z';
    buf.append(&ch23, 1);
    assert(buf.bytes() == 23);
    assert(buf.is_heap());
    assert(!buf.is_sbo());
    assert(buf.capacity() >= 23);
    assert(buf.c_str()[23] == '\0');

    // 4. Truncate in heap mode preserves null termination
    buf.truncate(15);
    assert(buf.bytes() == 15);
    assert(buf.is_heap()); // capacity retained
    assert(buf.c_str()[15] == '\0');

    // 5. Clear in heap mode preserves capacity and sets null terminator at 0
    sqlite3_int64 heap_cap = buf.capacity();
    buf.clear();
    assert(buf.bytes() == 0);
    assert(buf.capacity() == heap_cap);
    assert(buf.c_str()[0] == '\0');

    // 6. Reset releases heap and returns to SBO mode
    buf.reset();
    assert(buf.bytes() == 0);
    assert(buf.capacity() == 22);
    assert(buf.is_sbo());
    assert(!buf.is_heap());
    assert(buf.c_str()[0] == '\0');

    // 7. try_reserve() within SBO vs exceeding SBO
    SqliteBuffer res_buf;
    assert(res_buf.try_reserve(10).is_ok());
    assert(res_buf.is_sbo());
    assert(res_buf.capacity() == 22);

    assert(res_buf.try_reserve(22).is_ok());
    assert(res_buf.is_sbo());
    assert(res_buf.capacity() == 22);

    assert(res_buf.try_reserve(23).is_ok());
    assert(res_buf.is_heap());
    assert(res_buf.capacity() >= 23);
    assert(res_buf.c_str()[0] == '\0');

    // 8. append_uninitialized maintaining null termination across SBO boundary
    SqliteBuffer uninit_buf;
    void* p1 = uninit_buf.append_uninitialized(10);
    assert(p1 != nullptr);
    memset(p1, 'X', 10);
    assert(uninit_buf.is_sbo());
    assert(uninit_buf.c_str()[10] == '\0');

    void* p2 = uninit_buf.append_uninitialized(12); // total 22 (still SBO)
    assert(p2 != nullptr);
    memset(p2, 'Y', 12);
    assert(uninit_buf.is_sbo());
    assert(uninit_buf.bytes() == 22);
    assert(uninit_buf.c_str()[22] == '\0');

    void* p3 = uninit_buf.append_uninitialized(5); // total 27 (heap)
    assert(p3 != nullptr);
    memset(p3, 'Z', 5);
    assert(uninit_buf.is_heap());
    assert(uninit_buf.bytes() == 27);
    assert(uninit_buf.c_str()[27] == '\0');

    // 9. take() behavior on SBO and Heap
    SqliteBuffer sbo_src("hello", 5);
    assert(sbo_src.is_sbo());
    SqliteBuffer sbo_dst = sbo_src.take();
    assert(sbo_dst.is_sbo());
    assert(sbo_dst.bytes() == 5);
    assert(sbo_src.is_sbo());
    assert(sbo_src.bytes() == 0);
    assert(sbo_src.capacity() == 22);
    assert(sbo_src.c_str()[0] == '\0');

    SqliteBuffer heap_src("1234567890123456789012345", 25);
    assert(heap_src.is_heap());
    SqliteBuffer heap_dst = heap_src.take();
    assert(heap_dst.is_heap());
    assert(heap_dst.bytes() == 25);
    assert(heap_src.is_sbo());
    assert(heap_src.bytes() == 0);
    assert(heap_src.capacity() == 22);
    assert(heap_src.c_str()[0] == '\0');

    // 10. SqliteBuffer::SboTag direct contract & bitfield boundaries
    {
        static_assert(sizeof(SqliteBuffer::SboTag) == 1, "SboTag must be exactly 1 byte");
        SqliteBuffer::SboTag tag;
        tag.clear();
        assert(tag.is_sbo == 1);
        assert(tag.get() == 0);
        assert(tag.get_length() == 0);

        // Boundary: 0
        tag.set(0);
        assert(tag.is_sbo == 1);
        assert(tag.get() == 0);
        assert(tag.get_length() == 0);

        // Boundary: 1
        tag.set(1);
        assert(tag.is_sbo == 1);
        assert(tag.get() == 1);
        assert(tag.get_length() == 1);

        // Boundary: 21 (one below max SBO)
        tag.set(21);
        assert(tag.is_sbo == 1);
        assert(tag.get() == 21);
        assert(tag.get_length() == 21);

        // Boundary: 22 (maximum SBO capacity)
        tag.set(22);
        assert(tag.is_sbo == 1);
        assert(tag.get() == 22);
        assert(tag.get_length() == 22);

        // Alias set_length()
        tag.set_length(15);
        assert(tag.is_sbo == 1);
        assert(tag.get() == 15);
        assert(tag.get_length() == 15);

        tag.clear();
        assert(tag.is_sbo == 1);
        assert(tag.get() == 0);
    }

    // 11. Exact 21, 22, 23, 24 byte direct construction boundaries
    {
        const char* p21 = "123456789012345678901";      // 21 bytes
        const char* p22 = "1234567890123456789012";     // 22 bytes
        const char* p23 = "12345678901234567890123";    // 23 bytes
        const char* p24 = "123456789012345678901234";   // 24 bytes

        SqliteBuffer b21(p21, 21);
        assert(b21.bytes() == 21);
        assert(b21.is_sbo());
        assert(!b21.is_heap());
        assert(b21.capacity() == 22);
        assert(b21.c_str()[21] == '\0');
        assert(memcmp(b21.data(), p21, 21) == 0);

        SqliteBuffer b22(p22, 22);
        assert(b22.bytes() == 22);
        assert(b22.is_sbo());
        assert(!b22.is_heap());
        assert(b22.capacity() == 22);
        assert(b22.c_str()[22] == '\0');
        assert(memcmp(b22.data(), p22, 22) == 0);

        SqliteBuffer b23(p23, 23);
        assert(b23.bytes() == 23);
        assert(!b23.is_sbo());
        assert(b23.is_heap());
        assert(b23.capacity() >= 23);
        assert(b23.c_str()[23] == '\0');
        assert(memcmp(b23.data(), p23, 23) == 0);

        SqliteBuffer b24(p24, 24);
        assert(b24.bytes() == 24);
        assert(!b24.is_sbo());
        assert(b24.is_heap());
        assert(b24.capacity() >= 24);
        assert(b24.c_str()[24] == '\0');
        assert(memcmp(b24.data(), p24, 24) == 0);
    }

    // 12. Exact append_uninitialized step transitions (21 -> 22 -> 23)
    {
        SqliteBuffer step_buf;
        void* s1 = step_buf.append_uninitialized(21);
        assert(s1 != nullptr);
        memset(s1, 'A', 21);
        assert(step_buf.bytes() == 21);
        assert(step_buf.is_sbo());
        assert(step_buf.c_str()[21] == '\0');

        // Step to exact boundary 22 (still SBO)
        void* s2 = step_buf.append_uninitialized(1);
        assert(s2 != nullptr);
        memset(s2, 'B', 1);
        assert(step_buf.bytes() == 22);
        assert(step_buf.is_sbo());
        assert(step_buf.c_str()[22] == '\0');

        // Step over boundary to 23 (heap transition)
        void* s3 = step_buf.append_uninitialized(1);
        assert(s3 != nullptr);
        memset(s3, 'C', 1);
        assert(step_buf.bytes() == 23);
        assert(step_buf.is_heap());
        assert(step_buf.c_str()[23] == '\0');

        // Zero additional bytes edge case
        void* s0 = step_buf.append_uninitialized(0);
        assert(s0 == nullptr);
        assert(step_buf.bytes() == 23);
        assert(step_buf.c_str()[23] == '\0');
    }

    // 13. Exact try_reserve() and truncate() boundaries across SBO and Heap
    {
        SqliteBuffer r_buf;
        assert(r_buf.try_reserve(22).is_ok());
        assert(r_buf.is_sbo());
        assert(r_buf.capacity() == 22);

        // Reserve 23 (triggers heap transition)
        assert(r_buf.try_reserve(23).is_ok());
        assert(r_buf.is_heap());
        assert(r_buf.capacity() >= 23);
        assert(r_buf.c_str()[0] == '\0');

        // Append 23 bytes to buffer
        assert(r_buf.append("12345678901234567890123", 23));
        assert(r_buf.bytes() == 23);
        assert(r_buf.is_heap());

        // Truncate down to 22 on heap (preserves heap capacity, updates size)
        sqlite3_int64 cap_before = r_buf.capacity();
        r_buf.truncate(22);
        assert(r_buf.is_heap());
        assert(r_buf.bytes() == 22);
        assert(r_buf.capacity() == cap_before);
        assert(r_buf.c_str()[22] == '\0');

        // Truncate down to 0 on heap
        r_buf.truncate(0);
        assert(r_buf.is_heap());
        assert(r_buf.bytes() == 0);
        assert(r_buf.capacity() == cap_before);
        assert(r_buf.c_str()[0] == '\0');
    }

    // 14. SqliteString exact 22 vs 23 byte boundaries
    {
        SqliteString s22("1234567890123456789012");
        assert(s22.length() == 22);
        assert(s22.is_sbo());
        assert(!s22.is_heap());
        assert(s22.c_str()[22] == '\0');

        // Append 1 character to cross boundary
        s22.append('!');
        assert(s22.length() == 23);
        assert(s22.is_heap());
        assert(s22.c_str()[23] == '\0');
        assert(s22.c_str()[22] == '!');

        SqliteString s23("12345678901234567890123");
        assert(s23.length() == 23);
        assert(s23.is_heap());
        assert(!s23.is_sbo());
        assert(s23.c_str()[23] == '\0');
    }

    // 15. Embedded null bytes at boundary (22 bytes with nulls)
    {
        char raw[22];
        memset(raw, 0, sizeof(raw));
        raw[0] = 'H';
        raw[10] = 'X';
        raw[21] = 'Z';

        SqliteBuffer null_buf(raw, 22);
        assert(null_buf.is_sbo());
        assert(null_buf.bytes() == 22);
        assert(null_buf.c_str()[22] == '\0');
        assert(memcmp(null_buf.data(), raw, 22) == 0);
    }
}

void test_string_wrapper_and_operators() {
    printf("5. Testing SqliteString wrapper methods, indexing, views, and symmetric comparisons...\n");

    // 1. Single character append
    SqliteString str;
    for (int i = 0; i < 22; ++i) {
        str.append(static_cast<char>('A' + i));
    }
    assert(str.length() == 22);
    assert(str.is_sbo());
    assert(str.c_str()[22] == '\0');

    str.append('W'); // 23rd char -> heap transition
    assert(str.length() == 23);
    assert(str.is_heap());
    assert(str.c_str()[23] == '\0');

    // 2. Subscript operator (const and mutable)
    assert(str[0] == 'A');
    assert(str[21] == 'V');
    assert(str[22] == 'W');

    str[0] = 'Z';
    assert(str[0] == 'Z');
    assert(str.c_str()[0] == 'Z');
    assert(str.length() == 23);

    // 3. Symmetric C-string comparisons
    SqliteString sample("SQLite");
    assert(sample == "SQLite");
    assert("SQLite" == sample);
    assert(sample != "Postgres");
    assert("Postgres" != sample);
    assert(sample < "ZZZ");
    assert("AAA" < sample);
    assert(sample <= "SQLite");
    assert("SQLite" <= sample);
    assert(sample >= "SQLite");
    assert("SQLite" >= sample);

    // 4. Nullptr comparison symmetry
    SqliteString null_str;
    assert(null_str == nullptr);
    assert(nullptr == null_str);
    assert(!(null_str != nullptr));
    assert(!(nullptr != null_str));
    assert(null_str <= nullptr);
    assert(nullptr <= null_str);

    SqliteString non_null_str("data");
    assert(non_null_str != nullptr);
    assert(nullptr != non_null_str);
    assert(non_null_str > nullptr);
    assert(!(non_null_str == nullptr));

    // 5. SqliteStringView interoperability
    SqliteStringView sv = non_null_str.view();
    assert(sv.length() == 4);
    assert(memcmp(sv.data(), "data", 4) == 0);

    // 6. Slicing on string
    SqliteBufferSlice sl1 = non_null_str.bufferSlice(1, 2);
    assert(sl1.bytes() == 2);
    assert(sl1 == "at");

    SqliteBufferSlice sl_oob = non_null_str.bufferSlice(10, 5);
    assert(sl_oob.bytes() == 0);
    assert(sl_oob == nullptr);
}

void test_buffer_slice_comprehensive() {
    printf("6. Testing SqliteBufferSlice non-owning view semantics & edge cases...\n");

    // Default empty slice
    SqliteBufferSlice empty_slice;
    assert(empty_slice.bytes() == 0);
    assert(empty_slice.size() == 0);
    assert(empty_slice.empty());
    assert(empty_slice.data() == nullptr);
    assert(empty_slice == nullptr);
    assert(nullptr == empty_slice);
    assert(empty_slice == "");

    const char* raw = "Quick brown fox";
    SqliteBufferSlice slice(raw, 15);
    assert(slice.bytes() == 15);
    assert(!slice.empty());
    assert(slice.data() == raw);
    assert(slice != nullptr);
    assert(slice == "Quick brown fox");
    assert(slice != "Lazy dog");

    // Lexicographical comparisons
    SqliteBufferSlice slice_a("apple", 5);
    SqliteBufferSlice slice_b("banana", 6);
    assert(slice_a < slice_b);
    assert(slice_b > slice_a);
    assert(slice_a <= slice_b);
    assert(slice_b >= slice_a);
    assert(slice_a <= slice_a);
    assert(slice_a >= slice_a);
    assert(slice_a == slice_a);

    // Prefix ordering
    SqliteBufferSlice slice_app("app", 3);
    assert(slice_app < slice_a);
    assert(slice_a > slice_app);

    // Hash calculation consistency
    assert(slice_a.hash() == SqliteHashUtil::hash("apple", 5));
}

void test_bind_and_result() {
    printf("7. Testing SqliteBuffer, SqliteString, and SqliteBufferSlice bind() & result()...\n");

    sqlite3* db = nullptr;
    int rc = sqlite3_open(":memory:", &db);
    assert(rc == SQLITE_OK && db != nullptr);

    char* err_msg = nullptr;
    rc = sqlite3_exec(db, "CREATE TABLE test_items (id INT, blob_val BLOB, text_val TEXT);", nullptr, nullptr, &err_msg);
    assert(rc == SQLITE_OK);

    // 1. Test Statement Binding
    sqlite3_stmt* insert_stmt = nullptr;
    rc = sqlite3_prepare_v2(db, "INSERT INTO test_items VALUES (?, ?, ?);", -1, &insert_stmt, nullptr);
    assert(rc == SQLITE_OK && insert_stmt != nullptr);

    // Row 1: SBO Buffer (<= 22 bytes) + SBO String
    SqliteBuffer sbo_buf("small_blob_123", 14);
    assert(sbo_buf.is_sbo());
    SqliteString sbo_str("small_str_abc");
    assert(sbo_str.is_sbo());

    sqlite3_bind_int(insert_stmt, 1, 1);
    assert(sbo_buf.bind(insert_stmt, 2) == SQLITE_OK);
    assert(sbo_str.bind(insert_stmt, 3) == SQLITE_OK);
    assert(sqlite3_step(insert_stmt) == SQLITE_DONE);
    sqlite3_reset(insert_stmt);

    // Row 2: Heap Buffer (> 22 bytes) + Heap String
    SqliteBuffer heap_buf("large_binary_payload_crossing_sbo_boundary", 42);
    assert(heap_buf.is_heap());
    SqliteString heap_str("large_string_payload_crossing_sbo_boundary");
    assert(heap_str.is_heap());

    sqlite3_bind_int(insert_stmt, 1, 2);
    assert(heap_buf.bind(insert_stmt, 2) == SQLITE_OK);
    assert(heap_str.bind(insert_stmt, 3) == SQLITE_OK);
    assert(sqlite3_step(insert_stmt) == SQLITE_DONE);
    sqlite3_reset(insert_stmt);

    // Row 3: SqliteBufferSlice binding
    SqliteBufferSlice slice("slice_payload", 13);
    sqlite3_bind_int(insert_stmt, 1, 3);
    assert(slice.bind(insert_stmt, 2) == SQLITE_OK);
    assert(sbo_str.bind(insert_stmt, 3) == SQLITE_OK);
    assert(sqlite3_step(insert_stmt) == SQLITE_DONE);
    sqlite3_finalize(insert_stmt);

    // Verify rows in database
    sqlite3_stmt* select_stmt = nullptr;
    rc = sqlite3_prepare_v2(db, "SELECT id, blob_val, text_val FROM test_items ORDER BY id;", -1, &select_stmt, nullptr);
    assert(rc == SQLITE_OK);

    // Row 1 check
    assert(sqlite3_step(select_stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(select_stmt, 0) == 1);
    assert(sqlite3_column_bytes(select_stmt, 1) == 14);
    assert(memcmp(sqlite3_column_blob(select_stmt, 1), "small_blob_123", 14) == 0);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(select_stmt, 2)), "small_str_abc") == 0);

    // Row 2 check
    assert(sqlite3_step(select_stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(select_stmt, 0) == 2);
    assert(sqlite3_column_bytes(select_stmt, 1) == 42);
    assert(memcmp(sqlite3_column_blob(select_stmt, 1), "large_binary_payload_crossing_sbo_boundary", 42) == 0);
    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(select_stmt, 2)), "large_string_payload_crossing_sbo_boundary") == 0);

    // Row 3 check
    assert(sqlite3_step(select_stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(select_stmt, 0) == 3);
    assert(sqlite3_column_bytes(select_stmt, 1) == 13);
    assert(memcmp(sqlite3_column_blob(select_stmt, 1), "slice_payload", 13) == 0);
    sqlite3_finalize(select_stmt);

    // 2. Test UDF Result Setting
    auto udf_echo_buf = [](sqlite3_context* ctx, int, sqlite3_value** argv) {
        const void* ptr = sqlite3_value_blob(argv[0]);
        int len = sqlite3_value_bytes(argv[0]);
        SqliteBuffer ret_buf(ptr, len);
        ret_buf.result(ctx);
    };

    auto udf_echo_str = [](sqlite3_context* ctx, int, sqlite3_value** argv) {
        const unsigned char* txt = sqlite3_value_text(argv[0]);
        SqliteString ret_str(reinterpret_cast<const char*>(txt));
        ret_str.result(ctx);
    };

    auto udf_echo_slice = [](sqlite3_context* ctx, int, sqlite3_value** argv) {
        const void* ptr = sqlite3_value_blob(argv[0]);
        int len = sqlite3_value_bytes(argv[0]);
        SqliteBufferSlice ret_slice(ptr, len);
        ret_slice.result(ctx);
    };

    sqlite3_create_function(db, "echo_buf", 1, SQLITE_UTF8, nullptr, udf_echo_buf, nullptr, nullptr);
    sqlite3_create_function(db, "echo_str", 1, SQLITE_UTF8, nullptr, udf_echo_str, nullptr, nullptr);
    sqlite3_create_function(db, "echo_slice", 1, SQLITE_UTF8, nullptr, udf_echo_slice, nullptr, nullptr);

    sqlite3_stmt* udf_stmt = nullptr;
    rc = sqlite3_prepare_v2(db, "SELECT echo_buf(x'aabbcc'), echo_str('hello udf'), echo_slice(x'1122');", -1, &udf_stmt, nullptr);
    assert(rc == SQLITE_OK);
    assert(sqlite3_step(udf_stmt) == SQLITE_ROW);

    assert(sqlite3_column_bytes(udf_stmt, 0) == 3);
    assert(memcmp(sqlite3_column_blob(udf_stmt, 0), "\xaa\xbb\xcc", 3) == 0);

    assert(strcmp(reinterpret_cast<const char*>(sqlite3_column_text(udf_stmt, 1)), "hello udf") == 0);

    assert(sqlite3_column_bytes(udf_stmt, 2) == 2);
    assert(memcmp(sqlite3_column_blob(udf_stmt, 2), "\x11\x22", 2) == 0);

    sqlite3_finalize(udf_stmt);
    sqlite3_close(db);
}

void test_interop_comparison_operators() {
    printf("8. Testing newly added interop comparison operators...\n");

    // 1. SqliteBufferSlice vs SqliteBuffer operators
    {
        SqliteBuffer b1("hello", 5);
        SqliteBufferSlice s1("hello", 5);
        SqliteBufferSlice s_less("hella", 5);
        SqliteBufferSlice s_more("hellz", 5);
        SqliteBufferSlice s_short("hell", 4);

        assert(s1 == b1); assert(b1 == s1);
        assert(!(s1 != b1)); assert(!(b1 != s1));
        assert(s_less != b1); assert(b1 != s_less);
        assert(s_less < b1); assert(b1 > s_less);
        assert(s_more > b1); assert(b1 < s_more);
        assert(s_short < b1); assert(b1 > s_short);
        assert(s1 <= b1); assert(b1 >= s1);
        assert(s1 >= b1); assert(b1 <= s1);
    }

    // 2. SqliteBlobOwned vs SqliteBuffer and SqliteBufferSlice
    {
        SqliteBlobOwned blob1("payload", 7);
        SqliteBuffer buf_same("payload", 7);
        SqliteBuffer buf_diff("payloae", 7);
        SqliteBufferSlice slice_same("payload", 7);
        SqliteBufferSlice slice_diff("payloaa", 7);

        assert(blob1 == buf_same); assert(buf_same == blob1);
        assert(blob1 != buf_diff); assert(buf_diff != blob1);
        assert(blob1 < buf_diff); assert(buf_diff > blob1);
        assert(blob1 <= buf_diff); assert(buf_diff >= blob1);

        assert(blob1 == slice_same); assert(slice_same == blob1);
        assert(blob1 != slice_diff); assert(slice_diff != blob1);
        assert(slice_diff < blob1); assert(blob1 > slice_diff);
        assert(slice_diff <= blob1); assert(blob1 >= slice_diff);
    }

    // 3. Explicit constructors (SqliteBlobOwned & SqliteBlobView from Buffer / Slice)
    {
        SqliteBuffer src_buf("blob_src", 8);
        SqliteBlobOwned bo_from_buf(src_buf);
        assert(bo_from_buf.size() == 8);
        assert(bo_from_buf == src_buf);

        SqliteBufferSlice src_slice(src_buf);
        SqliteBlobOwned bo_from_slice(src_slice);
        assert(bo_from_slice.size() == 8);
        assert(bo_from_slice == src_slice);

        SqliteBlobView bv_from_buf(src_buf);
        assert(bv_from_buf.size() == 8);
        assert(memcmp(bv_from_buf.data(), "blob_src", 8) == 0);

        SqliteBlobView bv_from_slice(src_slice);
        assert(bv_from_slice.size() == 8);
        assert(memcmp(bv_from_slice.data(), "blob_src", 8) == 0);
    }

    // 4. SqliteValueOwned & SqliteValueView vs SqliteBuffer & SqliteBufferSlice
    sqlite3* db = nullptr;
    int rc = sqlite3_open(":memory:", &db);
    assert(rc == SQLITE_OK);

    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db, "SELECT x'626c6f625f737263', 'testing text';", -1, &stmt, nullptr);
    assert(rc == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);

    SqliteValueView view_blob = SqliteValueView::from_column(stmt, 0);
    SqliteValueView view_text = SqliteValueView::from_column(stmt, 1);

    {
        SqliteBuffer src_buf("blob_src", 8);
        SqliteBufferSlice src_slice(src_buf);

        SqliteValueOwned val_blob = SqliteValueOwned::from_blob(src_buf.data(), static_cast<int>(src_buf.bytes()));

        assert(val_blob == src_buf); assert(src_buf == val_blob);
        assert(val_blob == src_slice); assert(src_slice == val_blob);
        assert(view_blob == src_buf); assert(src_buf == view_blob);
        assert(view_blob == src_slice); assert(src_slice == view_blob);

        // Cross-type ordering in SQLite: NULL (0) < INTEGER/REAL (1) < TEXT (2) < BLOB (3)
        SqliteValueOwned val_null;
        SqliteValueOwned val_int(42);
        SqliteValueOwned val_txt("blob_src");

        assert(val_null < src_buf); assert(src_buf > val_null);
        assert(val_int < src_buf); assert(src_buf > val_int);
        assert(val_txt < src_buf); assert(src_buf > val_txt);
        assert(val_txt != src_buf);
        assert(src_buf != val_txt);
    }

    // 5. SqliteValueOwned & SqliteValueView vs SqliteString
    {
        SqliteString str_val("testing text");
        SqliteValueOwned val_text_owned("testing text");

        assert(val_text_owned == str_val); assert(str_val == val_text_owned);
        assert(view_text == str_val); assert(str_val == view_text);

        SqliteString str_other("testing uext");
        assert(val_text_owned != str_other); assert(str_other != val_text_owned);
        assert(val_text_owned < str_other); assert(str_other > val_text_owned);

        SqliteValueOwned val_null;
        SqliteValueOwned val_int(42);
        // NULL < INT < TEXT
        assert(val_null < str_val); assert(str_val > val_null);
        assert(val_int < str_val); assert(str_val > val_int);
    }

    // 6. Direct C-string comparison with SqliteValueOwned & SqliteValueView
    {
        SqliteValueOwned val_text_owned("testing text");

        assert(val_text_owned == "testing text"); assert("testing text" == val_text_owned);
        assert(view_text == "testing text"); assert("testing text" == view_text);
        assert(val_text_owned != "other"); assert("other" != val_text_owned);
        assert(val_text_owned > "abc"); assert("abc" < val_text_owned);
        assert(val_text_owned >= "testing text"); assert("testing text" <= val_text_owned);
        assert(val_text_owned <= "testing text"); assert("testing text" >= val_text_owned);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    // 7. Hash stability across Buffer, String, BufferSlice, and Value Types
    {
        SqliteString str("consistent_payload");
        SqliteBuffer buf("consistent_payload", 18);
        SqliteBufferSlice slice("consistent_payload", 18);
        SqliteStringView str_view("consistent_payload", 18);
        SqliteBlobView blob_view("consistent_payload", 18);
        SqliteBlobOwned blob_owned("consistent_payload", 18);

        uint64_t h_str = str.hash();
        uint64_t h_buf = buf.hash();
        uint64_t h_slice = slice.hash();
        uint64_t h_str_view = str_view.hash();
        uint64_t h_blob_view = blob_view.hash();
        uint64_t h_blob_owned = blob_owned.hash();

        assert(h_str == h_buf);
        assert(h_buf == h_slice);
        assert(h_slice == h_str_view);
        assert(h_str_view == h_blob_view);
        assert(h_blob_view == h_blob_owned);

        SqliteValueHash hasher;
        assert(hasher(str) == h_str);
        assert(hasher(buf) == h_buf);
        assert(hasher(slice) == h_slice);
    }
}

int main() {
    // sqlite3_malloc requires initialization
    sqlite3_initialize();

    test_buffer();
    test_string();
    test_try_methods();
    test_sbo_null_termination_and_transitions();
    test_string_wrapper_and_operators();
    test_buffer_slice_comprehensive();
    test_bind_and_result();
    test_interop_comparison_operators();

    sqlite3_shutdown();

    printf("\nAll 8 SqliteBuffer/SqliteString Test Suites Passed Successfully!\n");
    return 0;
}

