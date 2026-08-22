# Incremental BLOB I/O Streams (`sqlite3_blob_stream.hpp`)

This header provides a zero-allocation, exception-safe RAII wrapper for SQLite's Incremental BLOB I/O API (`sqlite3_blob_open`).

It allows you to treat a specific cell in a database row exactly like a file handle, enabling you to read or write massive BLOBs chunk-by-chunk without ever loading the entire byte array into memory!

## Core Features
1. **Zero-RAM Streaming**: Ideal for 500MB+ videos, files, or Massive GIN Posting Lists.
2. **Delta Updates**: Instantly seek and update 10 bytes in the middle of a 1GB blob without rewriting the entire row.
3. **RAII Safety**: Prevents locking bugs by guaranteeing that `sqlite3_blob_close` is automatically called.

## Usage Guide

### 1. Preparing the BLOB Row
Because SQLite does not allow you to dynamically "append" to a BLOB to change its total length via this API, you must first create an empty BLOB of the correct size using `zeroblob()`.

```cpp
SqliteDatabaseOwned db("my_app.sqlite");
db.exec("CREATE TABLE files (id INTEGER PRIMARY KEY, data BLOB);");

// Create a massive 500MB placeholder
SqliteTransaction txn(db);
txn.exec("INSERT INTO files (id, data) VALUES (1, zeroblob(500000000));");
txn.commit();
```

### 2. Streaming Data
Use `SqliteBlobStream` to open the specific row and start writing chunks.

```cpp
// 1 = The Row ID
// true = Open for write access
SqliteBlobStream stream(db, "main", "files", "data", 1, true);

if (!stream) {
    // Handle open failure
}

const char* chunk1 = "Hello";
const char* chunk2 = "World";

// Write chunks at specific offsets
stream.write(chunk1, 5, 0); // Write "Hello" at offset 0
stream.write(chunk2, 5, 5); // Write "World" at offset 5

// stream.close() is automatically called here!
```

### 3. Fast-Path Reopening
If you need to process multiple rows in the same table and column rapidly, do not destroy and recreate the `SqliteBlobStream`. Instead, use `.reopen(new_rowid)` to instantly redirect the stream to a different row!

```cpp
SqliteBlobStream stream(db, "main", "files", "data", 1, true);
// ... write to row 1 ...

// Instantly jump to Row 2 without closing and reopening the handle
stream.reopen(2);
// ... write to row 2 ...
```

### 4. Zero-Copy Integration with SqliteBuffer
If you are dynamically accumulating data into a `SqliteBuffer` in RAM, you can stream it directly into a blob, or use `append_uninitialized` to read directly from a blob into the buffer without extra memory copies!

```cpp
SqliteBlobStream stream(db, "main", "files", "data", 1, false);

SqliteBuffer buffer;
int total_size = stream.bytes();

// Reserve raw capacity and pull data directly from the BLOB into the buffer
void* raw_dest = buffer.append_uninitialized(total_size);
stream.read(raw_dest, total_size, 0);

// You can also write a Slice directly back into the stream!
stream.write(buffer.bufferSlice(0, 100), 0);
```
