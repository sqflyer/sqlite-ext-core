#define SQLITE_CORE
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <utility>
#include "sqlite3_db.hpp"
#include "sqlite3_blob_stream.hpp"
#include "sqlite3_transaction.hpp"
#include "sqlite3_buffer.hpp"

void test_blob_stream() {
    printf("1. Testing SqliteBlobStream read/write...\n");
    
    SqliteDatabaseOwned db(":memory:");
    db.exec("CREATE TABLE files (id INTEGER PRIMARY KEY, data BLOB);");
    
    {
        SqliteTransaction txn(db);
        // Create an empty 20-byte blob
        txn.exec("INSERT INTO files (id, data) VALUES (1, zeroblob(20));");
        txn.commit();
    }
    
    // Open writeable blob stream
    {
        SqliteBlobStream stream(db, "main", "files", "data", 1, true);
        assert(stream);
        assert(stream.bytes() == 20);
        
        const char* chunk1 = "Hello";
        const char* chunk2 = "World";
        
        assert(stream.write(chunk1, 5, 0) == SQLITE_OK);
        assert(stream.write(chunk2, 5, 5) == SQLITE_OK);
    } // Closes automatically
    
    // Open read-only blob stream
    {
        SqliteBlobStream stream(db, "main", "files", "data", 1, false);
        assert(stream);
        
        char buffer[11] = {0};
        assert(stream.read(buffer, 10, 0) == SQLITE_OK);
        assert(strcmp(buffer, "HelloWorld") == 0);
    }
}

void test_blob_reopen_and_move() {
    printf("2. Testing SqliteBlobStream reopen() and sqlite_move...\n");
    
    SqliteDatabaseOwned db(":memory:");
    db.exec("CREATE TABLE files (id INTEGER PRIMARY KEY, data BLOB);");
    
    db.exec("INSERT INTO files (id, data) VALUES (1, zeroblob(5));");
    db.exec("INSERT INTO files (id, data) VALUES (2, zeroblob(10));");
    
    SqliteBlobStream stream(db, "main", "files", "data", 1, true);
    assert(stream);
    assert(stream.bytes() == 5);
    
    // Fast-path jump to row 2 without closing the handle
    assert(stream.reopen(2) == SQLITE_OK);
    assert(stream.bytes() == 10);
    
    // Test Move Semantics
    SqliteBlobStream stream2(sqlite_move(stream));
    assert(!stream);
    assert(stream2);
    assert(stream2.bytes() == 10);
}

void test_blob_buffer_integration() {
    printf("3. Testing SqliteBlobStream + SqliteBuffer Integration...\n");
    
    SqliteDatabaseOwned db(":memory:");
    db.exec("CREATE TABLE files (id INTEGER PRIMARY KEY, data BLOB);");
    
    // Create an empty 20-byte blob
    db.exec("INSERT INTO files (id, data) VALUES (1, zeroblob(20));");
    
    // 1. Write from a SqliteBuffer directly into the BLOB
    {
        SqliteBlobStream stream(db, "main", "files", "data", 1, true);
        assert(stream);
        
        SqliteBuffer write_buf;
        write_buf.append("BufferData", 10);
        
        // Write the dynamic buffer directly to the stream starting at offset 0
        assert(stream.write(write_buf, 0) == SQLITE_OK);
    }
    
    // 2. Read from the BLOB directly into an auto-expanding SqliteBuffer
    {
        SqliteBlobStream stream(db, "main", "files", "data", 1, false);
        assert(stream);
        
        SqliteBuffer read_buf;
        // The buffer starts empty (capacity 0)
        assert(read_buf.bytes() == 0);
        
        // Read 10 bytes directly from offset 0 into the uninitialized tail of the buffer
        assert(stream.read(read_buf, 10, 0) == SQLITE_OK);
        
        // Ensure the buffer capacity auto-expanded and tracking updated
        assert(read_buf.bytes() == 10);
        assert(memcmp(read_buf.data(), "BufferData", 10) == 0);
        
        // 3. Test Rollback on Failed Read
        sqlite3_int64 pre_fail_size = read_buf.bytes();
        // Trying to read 100 bytes from offset 10 (Out of Bounds, since BLOB is only 20 bytes)
        assert(stream.read(read_buf, 100, 10) != SQLITE_OK);
        
        // Ensure the buffer was successfully truncated back to its original size to prevent corruption
        assert(read_buf.bytes() == pre_fail_size);
    }
}

void test_blob_buffer_advanced() {
    printf("4. Testing Advanced SqliteBlobStream + SqliteBuffer Scenarios...\n");
    
    SqliteDatabaseOwned db(":memory:");
    db.exec("CREATE TABLE files (id INTEGER PRIMARY KEY, data BLOB);");
    
    // Create an empty 50-byte blob
    db.exec("INSERT INTO files (id, data) VALUES (1, zeroblob(50));");
    
    SqliteBlobStream stream(db, "main", "files", "data", 1, true);
    assert(stream);
    
    // 1. Write from an empty buffer (should be a safe no-op)
    SqliteBuffer empty_buf;
    assert(stream.write(empty_buf, 0) == SQLITE_OK);
    
    // 2. Read 0 bytes into an empty buffer (should be a safe no-op)
    assert(stream.read(empty_buf, 0, 0) == SQLITE_OK);
    assert(empty_buf.bytes() == 0);
    
    // 3. Write data to the blob
    SqliteBuffer populate_buf;
    populate_buf.append("First Chunk|", 12);
    populate_buf.append("Second Chunk", 12);
    assert(stream.write(populate_buf, 0) == SQLITE_OK);
    
    // 4. Read into a PRE-POPULATED buffer (verify it appends without destroying existing data!)
    SqliteBuffer read_buf;
    read_buf.append("Header:", 7);
    
    // Read just the first chunk ("First Chunk|")
    assert(stream.read(read_buf, 12, 0) == SQLITE_OK);
    assert(read_buf.bytes() == 19);
    
    // Read the second chunk ("Second Chunk")
    assert(stream.read(read_buf, 12, 12) == SQLITE_OK);
    assert(read_buf.bytes() == 31);
    
    // Verify the entire concatenated payload
    assert(memcmp(read_buf.data(), "Header:First Chunk|Second Chunk", 31) == 0);
}

int main() {
    sqlite3_initialize();

    test_blob_stream();
    test_blob_reopen_and_move();
    test_blob_buffer_integration();
    test_blob_buffer_advanced();

    sqlite3_shutdown();

    printf("\nAll 4 SqliteBlobStream Test Suites Passed Successfully!\n");
    return 0;
}
