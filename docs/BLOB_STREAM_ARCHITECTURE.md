# BLOB Stream Architecture

The SQLite Incremental BLOB I/O subsystem (`SqliteBlobStream`) provides a high-performance C++ bridge to SQLite's `sqlite3_blob_*` family of functions.

## Architectural Justification

Standard SQL `INSERT` or `UPDATE` queries involving BLOBs force developers to bind a contiguous byte array to a prepared statement. For massive assets (like high-res images, videos, or Machine Learning weights), this forces the host machine to allocate an equivalent amount of contiguous RAM. 

For embedded systems or highly concurrent servers, this immediately causes out-of-memory (OOM) failures.

`SqliteBlobStream` circumvents the SQL execution engine entirely. By providing the exact table, column, and `rowid`, SQLite exposes a direct virtual file handle to the raw bytes on disk, bypassing the SQL query planner and the statement memory allocator.

## Zero-Cost RAII Design

The `SqliteBlobStream` wrapper adheres to our strict `-nostdlib++` constraints:

1. **No Memory Allocations**: It holds exactly one 8-byte pointer (`sqlite3_blob*`). No dynamic memory is used.
2. **Exception Safety**: The destructor guarantees that `sqlite3_blob_close` is called. Failing to close a blob handle leaves a long-lived read/write lock on the database, which will cause `SQLITE_BUSY` deadlocks on subsequent queries. RAII completely eliminates this class of bugs.
3. **Move Semantics**: The copy constructors are explicitly deleted to prevent double-free bugs (`SQLITE_MISUSE`). The stream can be safely passed across scopes using `sqlite_move`.
4. **Manual Override**: For complex threading scenarios where a lock must be yielded before the block scope exits, developers can call `.close()` manually. The destructor detects this and safely no-ops.

## Performance Heuristics

When designing systems using `SqliteBlobStream` (such as inverted indexes or vector databases), adhere to these performance constraints:

- **Preallocation**: You cannot expand the size of a BLOB using this API. The row must be pre-allocated using the `zeroblob(N)` SQL function.
- **Reopening**: Moving the `SqliteBlobStream` handle from row 1 to row 2 using `.reopen(2)` is significantly faster than destroying the stream and constructing a new one, as it bypasses the internal SQLite B-Tree traversal required to find the column offset.
- **Integration with SqliteBuffer**: `SqliteBlobStream` accepts and reads `SqliteBuffer` and `SqliteBufferSlice` directly, avoiding double-buffering during data extraction or persistence.
