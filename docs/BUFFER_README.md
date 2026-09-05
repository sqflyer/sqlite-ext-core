# Dynamic Strings and Buffers (`sqlite3_buffer.hpp`)

Because `sqlite-ext-core` strictly enforces `-nostdlib++`, standard containers like `std::string` and `std::vector` are not available.

This header provides two powerful, zero-dependency replacements that allocate dynamic memory directly from SQLite's internal memory arena (`sqlite3_malloc64` and `sqlite3_realloc64`).

## 1. `SqliteString` (The `std::string` replacement)

Use `SqliteString` whenever you need to dynamically build up a null-terminated C-string in RAM before sending it to SQLite.

```cpp
SqliteString str("Hello");

// Automatically reallocates to fit the new data!
str.append(" World"); 

// Use it exactly like a std::string!
if (str == "Hello World") {
    printf("Length: %lld\n", str.length());
    
    // Bind it to a statement! (Use SQLITE_TRANSIENT since str will clean itself up)
    stmt.bind(1, str.c_str());
}

// Full Relational Operators and Hashes!
SqliteString str2("hello");
if (str2 < "world") {
    printf("Hash: %llu\n", str2.hash());
}
```

## 2. `SqliteBuffer` (The `std::vector<uint8_t>` replacement)

Use `SqliteBuffer` whenever you need to dynamically buffer raw binary data in RAM (for example, accumulating chunks of a posting list before finally flushing it to disk via a `SqliteBlobStream`).

```cpp
SqliteBuffer buffer;

char chunk1[100];
char chunk2[50];

// Automatically resizes capacity geometrically (32 -> 64 -> 128...)
buffer.append(chunk1, 100);
buffer.append(chunk2, 50);

printf("Total Bytes Buffered: %lld\n", buffer.bytes());
printf("Total RAM Allocated: %lld\n", buffer.capacity());

// Clear the size without shrinking the underlying RAM capacity 
// (perfect for reusing the buffer in a tight loop!)
buffer.clear(); 

// Full Heterogeneous Equality and Hashing vs SqliteValueView!
assert(buffer.hash() == SqliteValueView(sqlite3_val).hash());
if (buffer == SqliteValueView(sqlite3_val)) {
    printf("Buffer is perfectly equal to SQLite Blob!");
}
```

## 3. Small Buffer Optimization (SBO) & Zero-Cost C-Strings

Both `SqliteBuffer` and `SqliteString` employ a **24-byte union** supporting Small Buffer Optimization (SBO):

- **Inline Stack Storage (0 to 22 bytes)**:
  Short strings, short BLOBs, identifiers, and UUIDs are stored **directly on the stack inside the 24-byte object**. Zero calls to `sqlite3_malloc64` or `sqlite3_free` are made!
- **Heap Storage (> 22 bytes)**:
  When data exceeds 22 bytes, the buffer seamlessly transitions to dynamic memory allocated from SQLite's memory arena with geometric capacity growth.
- **1-Bit Discriminator (`SboTag`)**:
  Byte 0 contains `SboTag`, where bit 0 distinguishes SBO (`is_sbo() == true`) from Heap (`is_heap() == true`), and bits 1..7 store the 7-bit length.
- **Guaranteed Null-Termination Invariant**:
  The byte immediately following active data (`data()[bytes()]`) is guaranteed to be `'\0'` across all states (empty, SBO, heap, clear, truncate). Calling `c_str()` is an $O(1)$, zero-copy operation that never reallocates.
- **`reset()`**:
  Releases any heap memory and returns the buffer back to empty SBO stack mode.

```cpp
SqliteString sbo_str("short_id"); // 8 bytes -> Inline SBO (0 heap allocations)
assert(sbo_str.is_sbo());
assert(sbo_str.capacity() == 22);

sbo_str.append("_and_now_exceeding_22_chars"); // -> Seamlessly transitions to heap
assert(sbo_str.is_heap());
assert(sbo_str.capacity() >= sbo_str.length());

sbo_str.reset(); // -> Releases heap, returns back to SBO mode
assert(sbo_str.is_sbo());
assert(sbo_str.length() == 0);
```

## 4. `SqliteBufferSlice` (The `std::span` / `std::string_view` replacement)

Use `SqliteBufferSlice` when you need to inspect or compare a piece of a buffer without triggering an expensive heap allocation or deep copy.

```cpp
SqliteBuffer buffer;
buffer.append("Hello World", 11);

// Extract a non-owning slice (zero heap allocations!)
SqliteBufferSlice slice = buffer.bufferSlice(0, 5);
assert(slice.bytes() == 5);
assert(slice == "Hello"); // Natively compares against C-Strings!
assert(slice < SqliteBufferSlice("World", 5));
```

## 5. OOM Safety & Validity Checking (`-fno-exceptions`)

Because `sqlite-ext-core` compiles with `-fno-exceptions`, memory allocation failures inside constructors or during dynamic resizing never throw. Both `SqliteBuffer` and `SqliteString` provide explicit validity checks:

```cpp
SqliteString str("Large text");
if (!str.is_valid()) {
    // Memory allocation failed (OOM condition)
}

// Explicit boolean operator conversion
if (str) {
    // String is valid and ready to use
}
```

## 6. Fallible APIs & `SqliteResult<SqliteString>`

For environments where memory limits are strictly enforced, `sqlite3_buffer.hpp` provides fallible methods returning [`SqliteResult`](file:///c:/msys64/home/dilipvamsi/works/repos/sqlite-ext-core/include/sqlite3_allocator.hpp#L882-L994) or [`SqliteStatus`](file:///c:/msys64/home/dilipvamsi/works/repos/sqlite-ext-core/include/sqlite3_allocator.hpp#L775-L863):

```cpp
// 1. Fallible string creation
SqliteResult<SqliteString> res = SqliteString::try_create("Initial text data");
if (res.is_err()) {
    return res.status();
}
SqliteString str = res.take_value();

// 2. Fallible buffer operations
SqliteBuffer buf;
SqliteStatus reserve_stat = buf.try_reserve(1024);
if (reserve_stat.is_err()) {
    return reserve_stat;
}

SqliteStatus append_stat = buf.try_append(data, data_len);
if (append_stat.is_err()) {
    printf("Buffer append failed [%d]: %s\n", append_stat.err_code(), append_stat.err_msg());
}
```
