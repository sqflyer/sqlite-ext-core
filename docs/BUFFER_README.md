# Dynamic Strings and Buffers (`sqlite3_buffer.hpp`)

Because `c-sqlite-ext-core` strictly enforces `-nostdlib++`, standard containers like `std::string` and `std::vector` are not available.

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
