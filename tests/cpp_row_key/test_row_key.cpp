#define SQLITE_CORE
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sqlite3.h>
#include "sqlite3_row.hpp"
#include "sqlite3_row_key.hpp"

void test_row_owned_wrapper_scalars() {
    printf("Running test_row_owned_wrapper_scalars...\n");

    SqliteValueOwned val_int(42);
    SqliteRowOwnedWrapper view_int(val_int);
    assert(view_int.size() == 1);
    assert(!view_int.empty());
    assert(view_int[0].as_int64() == 42);
    assert(view_int == val_int);
    assert(view_int.hash() == val_int.hash());

    SqliteValueOwned val_str("hello");
    SqliteRowOwnedWrapper view_str(val_str);
    assert(view_str.size() == 1);
    assert(strcmp(view_str[0].as_text().data(), "hello") == 0);
    assert(view_str == val_str);
    assert(view_str.hash() == val_str.hash());
}

void test_row_key_owned_sbo() {
    printf("Running test_row_key_owned_sbo...\n");

    // SBO 1-element key: exactly 16 bytes
    assert(sizeof(SqliteRowKeyOwned) == 16);
    assert(sizeof(SqliteRowOwnedWrapper) == 16);

    SqliteValueOwned val(123456789LL);
    SqliteRowKeyOwned key1(val);
    assert(key1.size() == 1);
    assert(!key1.empty());
    assert(key1[0].as_int64() == 123456789LL);
    assert(key1.hash() == val.hash());
    assert(key1 == val);

    // RowOwnedWrapper from RowKeyOwned
    SqliteRowOwnedWrapper view1 = key1.view();
    assert(view1.size() == 1);
    assert(view1 == key1);
    assert(view1 == val);

    // Copy semantics
    SqliteRowKeyOwned key2 = key1;
    assert(key2 == key1);
    assert(key2[0].as_int64() == 123456789LL);

    // Move semantics
    SqliteRowKeyOwned key3 = sqlite_move(key2);
    assert(key3 == key1);
    assert(key3[0].as_int64() == 123456789LL);
}

void test_row_key_owned_composite() {
    printf("Running test_row_key_owned_composite...\n");

    // Construct composite key with 3 columns: (10, "test", 3.14)
    SqliteRowDynamic row(3);
    row[0] = SqliteValueOwned(10);
    row[1] = SqliteValueOwned("test");
    row[2] = SqliteValueOwned(3.14);

    int key_indices[] = {0, 1, 2};
    SqliteRowKeyOwned key_comp(row, key_indices, 3);
    assert(key_comp.size() == 3);
    assert(!key_comp.empty());
    assert(key_comp[0].as_int64() == 10);
    assert(strcmp(key_comp[1].as_text().data(), "test") == 0);
    assert(key_comp[2].as_double() == 3.14);

    // RowOwnedWrapper from composite key
    SqliteRowOwnedWrapper view_comp = key_comp.view();
    assert(view_comp.size() == 3);
    assert(view_comp == key_comp);
    assert(view_comp[0].as_int64() == 10);
    assert(strcmp(view_comp[1].as_text().data(), "test") == 0);

    // Copy composite
    SqliteRowKeyOwned key_comp_copy = key_comp;
    assert(key_comp_copy == key_comp);
    assert(key_comp_copy.size() == 3);

    // Move composite
    SqliteRowKeyOwned key_comp_moved = sqlite_move(key_comp_copy);
    assert(key_comp_moved == key_comp);
    assert(key_comp_moved.size() == 3);

    // Partial key extraction: indices {2, 0}
    int sub_indices[] = {2, 0};
    SqliteRowKeyOwned key_sub(row, sub_indices, 2);
    assert(key_sub.size() == 2);
    assert(key_sub[0].as_double() == 3.14);
    assert(key_sub[1].as_int64() == 10);

    // Test direct typed as_* accessors on SqliteRowKeyOwned
    assert(key_comp.as_int64(0) == 10);
    assert(key_comp.as_int(0) == 10);
    assert(key_comp.as_text(1) == "test");
    assert(key_comp.as_double(2) == 3.14);
    assert(!key_comp.is_null(0));
    assert(key_comp.type(0) == SQLITE_INTEGER);
    assert(key_comp.type(1) == SQLITE_TEXT);
    assert(key_comp.type(2) == SQLITE_FLOAT);
}

void test_key_ordering() {
    printf("Running test_key_ordering...\n");

    SqliteRowKeyOwned k1(SqliteValueOwned(10));
    SqliteRowKeyOwned k2(SqliteValueOwned(20));
    SqliteRowKeyOwned k1_copy(SqliteValueOwned(10));

    // Full relational comparisons on SqliteRowKeyOwned
    assert(k1 == k1_copy);
    assert(k1 != k2);
    assert(k1 < k2);
    assert(k1 <= k2);
    assert(k1 <= k1_copy);
    assert(k2 > k1);
    assert(k2 >= k1);
    assert(k1 >= k1_copy);

    SqliteRowOwnedWrapper v1 = k1.view();
    SqliteRowOwnedWrapper v2 = k2.view();

    // Full relational comparisons on SqliteRowOwnedWrapper
    assert(v1 == v1);
    assert(v1 != v2);
    assert(v1 < v2);
    assert(v1 <= v2);
    assert(v2 > v1);
    assert(v2 >= v1);

    // Cross Key vs Wrapper
    assert(v1 < k2);
    assert(v1 <= k2);
    assert(k1 < v2);
    assert(k1 <= v2);
    assert(k2 > v1);
    assert(k2 >= v1);
    assert(v2 > k1);
    assert(v2 >= k1);

    // Scalar comparisons
    SqliteValueOwned sc10(10);
    SqliteValueOwned sc20(20);
    assert(k1 == sc10);
    assert(k1 != sc20);
    assert(k1 < sc20);
    assert(k1 <= sc20);
    assert(k1 <= sc10);
    assert(k2 > sc10);
    assert(k2 >= sc10);

    assert(v1 == sc10);
    assert(v1 != sc20);
    assert(v1 < sc20);
    assert(v1 <= sc20);
    assert(v2 > sc10);
    assert(v2 >= sc10);
}

void test_single_vs_multiple_lexicographical_ordering() {
    printf("Running test_single_vs_multiple_lexicographical_ordering...\n");

    // 1. Single value (10) vs Multiple value (10, 20)
    // Rule: Shorter prefix is LESS than longer prefix (same as "a" < "ab")
    SqliteRowKeyOwned single_k(SqliteValueOwned(10));

    SqliteRowDynamic multi_row(2);
    multi_row[0] = SqliteValueOwned(10);
    multi_row[1] = SqliteValueOwned(20);
    int indices[] = {0, 1};
    SqliteRowKeyOwned multi_k(multi_row, indices, 2);

    // Single (10) < Multi (10, 20)
    assert(single_k < multi_k);
    assert(single_k <= multi_k);
    assert(multi_k > single_k);
    assert(multi_k >= single_k);
    assert(single_k != multi_k);
    assert(!(single_k == multi_k));
    assert(!(single_k > multi_k));
    assert(!(multi_k < single_k));

    // Via SqliteRowOwnedWrapper spans
    SqliteRowOwnedWrapper single_v = single_k.view();
    SqliteRowOwnedWrapper multi_v = multi_k.view();

    assert(single_v < multi_v);
    assert(single_v <= multi_v);
    assert(multi_v > single_v);
    assert(multi_v >= single_v);
    assert(single_v != multi_v);

    // 2. Direct comparison with scalar primitive / SqliteValueOwned(10)
    SqliteValueOwned sc10(10);
    assert(multi_k > sc10);
    assert(multi_k >= sc10);
    assert(sc10 < multi_k);
    assert(sc10 <= multi_k);
    assert(multi_k != sc10);

    assert(multi_v > sc10);
    assert(multi_v >= sc10);
    assert(sc10 < multi_v);
    assert(sc10 <= multi_v);

    // 3. Single value (15) vs Multiple value (10, 20)
    // Rule: 15 > 10, so (15) > (10, 20)
    SqliteRowKeyOwned single_15(SqliteValueOwned(15));
    assert(single_15 > multi_k);
    assert(multi_k < single_15);

    // 4. Single value (5) vs Multiple value (10, 20)
    // Rule: 5 < 10, so (5) < (10, 20)
    SqliteRowKeyOwned single_5(SqliteValueOwned(5));
    assert(single_5 < multi_k);
    assert(multi_k > single_5);

    // 5. String tuple: ("a") vs ("a", "b")
    SqliteRowDynamic str_row(2);
    str_row[0] = SqliteValueOwned("a");
    str_row[1] = SqliteValueOwned("b");
    SqliteRowKeyOwned multi_str(str_row, indices, 2);
    SqliteStringView str_a("a", 1);

    assert(str_a < multi_str);
    assert(multi_str > str_a);
    assert(multi_str != str_a);
}

void test_functors() {
    printf("Running test_functors...\n");

    SqliteRowKeyHash hasher;
    SqliteRowKeyEqual eq;
    SqliteRowKeyLess less;

    SqliteRowKeyOwned k1(SqliteValueOwned("abc"));
    SqliteRowKeyOwned k2(SqliteValueOwned("abc"));
    SqliteRowKeyOwned k3(SqliteValueOwned("xyz"));

    assert(hasher(k1) == hasher(k2));
    assert(eq(k1, k2));
    assert(!eq(k1, k3));
    assert(less(k1, k3));
    assert(!less(k3, k1));

    SqliteRowOwnedWrapper v1 = k1.view();
    assert(hasher(v1) == hasher(k1));
    assert(eq(v1, k1));
    assert(eq(k1, v1));
}

void test_murmurhash2_direct() {
    printf("Running test_murmurhash2_direct (exhaustive edge cases)...\n");

    // 1. Basic deterministic hashing
    const char* str = "hello_world_key";
    uint64_t h1 = SqliteHashUtil::hash(str, static_cast<int>(strlen(str)));
    uint64_t h2 = SqliteHashUtil::hash(str, static_cast<int>(strlen(str)));
    assert(h1 == h2);
    assert(h1 != 0);

    // 2. Null pointer & zero / negative length safety
    uint64_t h_null = SqliteHashUtil::hash(nullptr, 0);
    uint64_t h_empty = SqliteHashUtil::hash("", 0);
    uint64_t h_neg = SqliteHashUtil::hash(str, -5);
    assert(h_null == h_empty);
    assert(h_neg == h_null);

    // 3. Exhaustive buffer length boundaries (1..64 bytes) to exercise all tail switch cases
    char buffer[128];
    for (int i = 0; i < 128; ++i) buffer[i] = static_cast<char>('A' + (i % 26));

    uint64_t prev_hash = 0;
    for (int len = 1; len <= 64; ++len) {
        uint64_t h_len = SqliteHashUtil::hash(buffer, len);
        assert(h_len != 0);
        assert(h_len != prev_hash); // Each length must produce a distinct hash
        prev_hash = h_len;
    }

    // 4. Unaligned memory safety (pointers offset by 1, 2, 3, 5, 7 bytes)
    for (int offset = 1; offset < 8; ++offset) {
        const char* unaligned_ptr = buffer + offset;
        uint64_t h_unaligned = SqliteHashUtil::hash(unaligned_ptr, 16);
        assert(h_unaligned != 0);
        // Compare with same data in an aligned buffer
        char aligned_buf[16];
        memcpy(aligned_buf, unaligned_ptr, 16);
        uint64_t h_aligned = SqliteHashUtil::hash(aligned_buf, 16);
        assert(h_unaligned == h_aligned);
    }

    // 5. Floating point +0.0 vs -0.0 normalization
    uint64_t h_pos_zero = SqliteHashUtil::hash_double(+0.0);
    uint64_t h_neg_zero = SqliteHashUtil::hash_double(-0.0);
    assert(h_pos_zero == h_neg_zero);

    uint64_t h_pi = SqliteHashUtil::hash_double(3.1415926535);
    assert(h_pi != h_pos_zero);

    // 6. Direct 64-bit integer hashing
    uint64_t h_int1 = SqliteHashUtil::hash_int64(42);
    uint64_t h_int2 = SqliteHashUtil::hash_int64(42);
    uint64_t h_int3 = SqliteHashUtil::hash_int64(43);
    assert(h_int1 == h_int2);
    assert(h_int1 != h_int3);

    // 7. Incremental mix() utility
    uint64_t h_mix1 = SqliteHashUtil::mix(SqliteHashUtil::DEFAULT_SEED, "hello", 5);
    uint64_t h_direct = SqliteHashUtil::hash("hello", 5);
    assert(h_mix1 == h_direct);

    // 8. Combiner properties (non-commutative and non-cancelling)
    uint64_t c1 = SqliteHashUtil::combine(h1, h_pi);
    uint64_t c2 = SqliteHashUtil::combine(h_pi, h1);
    assert(c1 != c2); // Non-commutative: combine(A, B) != combine(B, A)
    assert(SqliteHashUtil::combine(h1, h1) != 0); // No self-cancellation
}

void test_bloom_hash_index() {
    printf("Running test_bloom_hash_index (bounds & uniformity)...\n");

    const char* key = "user_pk_42";
    uint64_t h = SqliteHashUtil::hash(key, static_cast<int>(strlen(key)));

    // 1. Standard probe index calculation
    const size_t num_bits = 10000;
    size_t probe0 = SqliteHashUtil::bloom_hash_index(h, 0, num_bits);
    size_t probe1 = SqliteHashUtil::bloom_hash_index(h, 1, num_bits);
    size_t probe2 = SqliteHashUtil::bloom_hash_index(h, 2, num_bits);
    size_t probe3 = SqliteHashUtil::bloom_hash_index(h, 3, num_bits);

    assert(probe0 < num_bits);
    assert(probe1 < num_bits);
    assert(probe2 < num_bits);
    assert(probe3 < num_bits);

    assert(probe0 != probe1 || probe1 != probe2);

    // 2. Zero and 1-bit boundary safety
    assert(SqliteHashUtil::bloom_hash_index(h, 0, 0) == 0);
    assert(SqliteHashUtil::bloom_hash_index(h, 5, 0) == 0);
    assert(SqliteHashUtil::bloom_hash_index(h, 3, 1) == 0);

    // 3. Uniform bounds for large number of probes (k = 64)
    for (uint32_t i = 0; i < 64; ++i) {
        size_t bit_idx = SqliteHashUtil::bloom_hash_index(h, i, num_bits);
        assert(bit_idx < num_bits);
    }
}

void test_factory_and_scope_dispatcher() {
    printf("Running test_factory_and_scope_dispatcher...\n");

    // 1. Static factory methods
    SqliteValueOwned val_sc(999);
    SqliteRowOwnedWrapper w1 = SqliteRowOwnedWrapper::create(val_sc);
    assert(w1.size() == 1);
    assert(w1[0].as_int64() == 999);

    SqliteValueOwnedStaticArray<3> s_arr;
    s_arr[0] = SqliteValueOwned(1);
    s_arr[1] = SqliteValueOwned(2);
    s_arr[2] = SqliteValueOwned(3);
    SqliteRowOwnedWrapper w2 = SqliteRowOwnedWrapper::create(s_arr);
    assert(w2.size() == 3);
    assert(w2[1].as_int64() == 2);

    // 2. withSqliteRowOwned runtime stack dispatcher (1..8)
    int pk_count = 3;
    int result = withSqliteRowOwned(pk_count, [](SqliteRowOwnedWrapper key_wrapper) {
        assert(key_wrapper.size() == 3);
        key_wrapper[0] = SqliteValueOwned(100);
        key_wrapper[1] = SqliteValueOwned(200);
        key_wrapper[2] = SqliteValueOwned(300);

        assert(key_wrapper[0].as_int64() == 100);
        assert(key_wrapper[1].as_int64() == 200);
        assert(key_wrapper[2].as_int64() == 300);
        return 42;
    });
    assert(result == 42);

    // 3. withSqliteRowOwned fallback for >8
    int large_count = 10;
    bool large_ok = withSqliteRowOwned(large_count, [](SqliteRowOwnedWrapper key_wrapper) {
        assert(key_wrapper.size() == 10);
        for (int i = 0; i < 10; ++i) {
            key_wrapper[i] = SqliteValueOwned(i * 10);
            assert(key_wrapper[i].as_int64() == i * 10);
        }
        return true;
    });
    assert(large_ok);
}

void test_strings_and_blobs() {
    printf("Running test_strings_and_blobs...\n");

    // 1. SqliteStringView and SqliteStringOwned
    SqliteStringView sv("apple", 5);
    SqliteRowKeyOwned k_str(sv);
    assert(k_str.size() == 1);
    assert(k_str == sv);
    assert(k_str.hash() == sv.hash());

    SqliteStringOwned so("banana");
    SqliteRowKeyOwned k_str2(so);
    assert(k_str2 == so);
    assert(k_str < k_str2);
    assert(k_str < so);
    assert(k_str2 > sv);

    SqliteRowOwnedWrapper w_str = k_str.view();
    assert(w_str == sv);
    assert(w_str < so);

    // 2. SqliteBlobView and SqliteBlobOwned
    const uint8_t raw_blob[] = {0xDE, 0xAD, 0xBE, 0xEF};
    SqliteBlobView bv(raw_blob, 4);
    SqliteRowKeyOwned k_blob(bv);
    assert(k_blob.size() == 1);
    assert(k_blob == bv);
    assert(k_blob.hash() == bv.hash());

    SqliteBlobOwned bo(raw_blob, 4);
    SqliteRowKeyOwned k_blob2(bo);
    assert(k_blob == k_blob2);
    assert(k_blob == bo);

    SqliteRowOwnedWrapper w_blob = k_blob.view();
    assert(w_blob == bv);
    assert(w_blob == bo);

    // 3. Transparent Functors with Strings & Blobs
    SqliteRowKeyHash hasher;
    assert(hasher(k_str) == hasher(sv));
    assert(hasher(k_blob) == hasher(bv));
}

void test_native_primitive_comparisons() {
    printf("Running test_native_primitive_comparisons...\n");

    // 1. Integer (int & sqlite3_int64)
    SqliteRowKeyOwned k_int(SqliteValueOwned(42));
    assert(k_int == 42);
    assert(k_int != 100);
    assert(k_int < 100);
    assert(k_int <= 42);
    assert(k_int > 10);
    assert(k_int >= 42);

    // Symmetric reverse operators for int
    assert(42 == k_int);
    assert(100 != k_int);
    assert(100 > k_int);
    assert(10 < k_int);
    assert(42 <= k_int);
    assert(42 >= k_int);

    // 64-bit int
    sqlite3_int64 big_num = 9000000000LL;
    SqliteValueOwned big_val(big_num);
    SqliteRowKeyOwned k_big(big_val);
    assert(k_big == big_num);
    assert(big_num == k_big);

    // Via SqliteRowOwnedWrapper
    SqliteRowOwnedWrapper w_int = k_int.view();
    assert(w_int == 42);
    assert(w_int < 100);
    assert(42 == w_int);
    assert(10 < w_int);

    // 2. C-String literal (const char*)
    SqliteRowKeyOwned k_str(SqliteValueOwned("hello"));
    assert(k_str == "hello");
    assert(k_str != "world");
    assert(k_str < "world");
    assert(k_str <= "hello");
    assert(k_str > "abc");
    assert(k_str >= "hello");

    // Symmetric reverse operators for const char*
    assert("hello" == k_str);
    assert("world" != k_str);
    assert("world" > k_str);
    assert("abc" < k_str);
    assert("hello" <= k_str);
    assert("hello" >= k_str);

    // Via SqliteRowOwnedWrapper
    SqliteRowOwnedWrapper w_str = k_str.view();
    assert(w_str == "hello");
    assert(w_str < "world");
    assert("hello" == w_str);
    assert("abc" < w_str);

    // 3. Floating-point (double)
    SqliteRowKeyOwned k_dbl(SqliteValueOwned(3.14159));
    assert(k_dbl == 3.14159);
    assert(k_dbl < 4.0);
    assert(k_dbl > 2.0);
    assert(3.14159 == k_dbl);
    assert(4.0 > k_dbl);
    assert(2.0 < k_dbl);

    // 4. Boolean (bool)
    SqliteRowKeyOwned k_bool(SqliteValueOwned(true));
    assert(k_bool == true);
    assert(k_bool != false);
    assert(true == k_bool);
    assert(false != k_bool);

    // 5. Transparent Functors with Native Types
    SqliteRowKeyHash hasher;
    assert(hasher(k_int) == hasher(42));
    assert(hasher(k_str) == hasher("hello"));
    assert(hasher(k_dbl) == hasher(3.14159));
}

void test_iterators() {
    printf("Running test_iterators (range-based for loops)...\n");

    // 1. SqliteRowKeyOwned (Scalar SBO)
    SqliteRowKeyOwned k_scalar(SqliteValueOwned(100));
    int count = 0;
    for (const SqliteValueOwned& val : k_scalar) {
        assert(val.as_int() == 100);
        count++;
    }
    assert(count == 1);

    // 2. SqliteRowKeyOwned (Composite)
    int cols[3] = {0, 1, 2};
    SqliteRowDynamic row(3);
    row[0] = SqliteValueOwned(10);
    row[1] = SqliteValueOwned(20);
    row[2] = SqliteValueOwned(30);
    SqliteRowKeyOwned k_comp(row, cols, 3);
    int sum = 0;
    count = 0;
    for (const SqliteValueOwned& val : k_comp) {
        sum += val.as_int();
        count++;
    }
    assert(count == 3);
    assert(sum == 60);

    // 3. SqliteRowOwnedWrapper
    SqliteRowOwnedWrapper wrap = row.view();
    sum = 0;
    for (const SqliteValueOwned& val : wrap) {
        sum += val.as_int();
    }
    assert(sum == 60);

    // 4. SqliteValueOwnedStaticArray<3>
    SqliteValueOwnedStaticArray<3> s_arr;
    s_arr[0] = SqliteValueOwned(1);
    s_arr[1] = SqliteValueOwned(2);
    s_arr[2] = SqliteValueOwned(3);
    sum = 0;
    for (const SqliteValueOwned& val : s_arr) {
        sum += val.as_int();
    }
    assert(sum == 6);

    // 5. SqliteValueOwnedDynamicArray
    SqliteValueOwnedDynamicArray d_arr(2);
    d_arr[0] = SqliteValueOwned("hello");
    d_arr[1] = SqliteValueOwned("world");
    count = 0;
    for (const SqliteValueOwned& val : d_arr) {
        assert(val.type() == SQLITE_TEXT);
        count++;
    }
    assert(count == 2);
}

int main() {
    printf("=== Starting sqlite3_row_key tests ===\n");
    test_murmurhash2_direct();
    test_bloom_hash_index();
    test_row_owned_wrapper_scalars();
    test_row_key_owned_sbo();
    test_row_key_owned_composite();
    test_key_ordering();
    test_single_vs_multiple_lexicographical_ordering();
    test_functors();
    test_factory_and_scope_dispatcher();
    test_strings_and_blobs();
    test_native_primitive_comparisons();
    test_iterators();
    printf("=== All sqlite3_row_key tests passed successfully! ===\n");
    return 0;
}
