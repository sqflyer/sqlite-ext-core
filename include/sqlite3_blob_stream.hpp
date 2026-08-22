#ifndef SQLITE3_BLOB_STREAM_H
#define SQLITE3_BLOB_STREAM_H

#include "sqlite3ext.h"
#include "sqlite3_db.hpp"
#include "sqlite3_buffer.hpp"

/**
 * @brief Zero-cost RAII wrapper for SQLite Incremental BLOB I/O.
 * 
 * Allows reading and writing massive BLOBs seamlessly chunk-by-chunk 
 * directly from disk without loading them into memory.
 */
class SqliteBlobStream {
    sqlite3_blob* m_blob;

public:
    /**
     * @brief Opens a stream to a specific cell in the database.
     * 
     * @param db The database connection.
     * @param db_name The database name (usually "main").
     * @param table The name of the table.
     * @param column The name of the BLOB column.
     * @param rowid The precise row ID to open.
     * @param writeable True if you want to write, false for read-only.
     */
    inline SqliteBlobStream(SqliteDatabaseView db, const char* db_name, const char* table, const char* column, sqlite3_int64 rowid, bool writeable = false) 
        : m_blob(nullptr) 
    {
        if (db) {
            sqlite3_blob_open(db, db_name, table, column, rowid, writeable ? 1 : 0, &m_blob);
        }
    }

    /**
     * @brief Automatically closes the blob handle.
     */
    inline ~SqliteBlobStream() {
        close();
    }

    // Non-copyable
    SqliteBlobStream(const SqliteBlobStream&) = delete;
    SqliteBlobStream& operator=(const SqliteBlobStream&) = delete;

    // Moveable
    inline SqliteBlobStream(SqliteBlobStream&& other) noexcept : m_blob(other.m_blob) {
        other.m_blob = nullptr;
    }

    inline SqliteBlobStream& operator=(SqliteBlobStream&& other) noexcept {
        if (this != &other) {
            close();
            m_blob = other.m_blob;
            other.m_blob = nullptr;
        }
        return *this;
    }

    /**
     * @brief Manually closes the blob handle early.
     * @return SQLITE_OK on success.
     */
    inline int close() {
        int rc = SQLITE_OK;
        if (m_blob) {
            rc = sqlite3_blob_close(m_blob);
            m_blob = nullptr;
        }
        return rc;
    }

    /** @brief Returns true if the stream successfully opened. */
    inline explicit operator bool() const {
        return m_blob != nullptr;
    }

    /** @brief Returns the total size of the BLOB in bytes. */
    inline int bytes() const {
        return m_blob ? sqlite3_blob_bytes(m_blob) : 0;
    }

    /**
     * @brief Reads data from the BLOB into a buffer.
     * 
     * @param buffer The buffer to write into.
     * @param n The number of bytes to read.
     * @param offset The zero-based offset into the BLOB to start reading.
     * @return SQLITE_OK on success.
     */
    inline int read(void* buffer, int n, int offset) const {
        return m_blob ? sqlite3_blob_read(m_blob, buffer, n, offset) : SQLITE_MISUSE;
    }

    /**
     * @brief Writes data from a buffer into the BLOB.
     * 
     * @param buffer The buffer to read from.
     * @param n The number of bytes to write.
     * @param offset The zero-based offset into the BLOB to start writing.
     * @return SQLITE_OK on success.
     */
    inline int write(const void* buffer, int n, int offset) {
        if (n <= 0) return SQLITE_OK;
        return m_blob ? sqlite3_blob_write(m_blob, buffer, n, offset) : SQLITE_MISUSE;
    }

    /**
     * @brief Reads data from the BLOB directly into an auto-expanding SqliteBuffer.
     * 
     * @param buffer The dynamic buffer to append the data into.
     * @param n The number of bytes to read.
     * @param offset The zero-based offset into the BLOB to start reading.
     * @return SQLITE_OK on success.
     */
    inline int read(SqliteBuffer& buffer, int n, int offset) const {
        if (!m_blob) return SQLITE_MISUSE;
        if (n <= 0) return SQLITE_OK;
        
        sqlite3_int64 original_size = buffer.bytes();
        void* dest = buffer.append_uninitialized(n);
        if (!dest) return SQLITE_NOMEM;
        
        int rc = sqlite3_blob_read(m_blob, dest, n, offset);
        if (rc != SQLITE_OK) {
            buffer.truncate(original_size); // Rollback on failure
        }
        return rc;
    }

    /**
     * @brief Writes data directly from a SqliteBuffer into the BLOB.
     * 
     * @param buffer The dynamic buffer to read from.
     * @param offset The zero-based offset into the BLOB to start writing.
     * @return SQLITE_OK on success.
     */
    inline int write(const SqliteBuffer& buffer, int offset) {
        return write(buffer.data(), static_cast<int>(buffer.bytes()), offset);
    }

    /**
     * @brief Writes data directly from a SqliteBufferSlice into the BLOB.
     * 
     * @param slice The non-owning memory slice to read from.
     * @param offset The zero-based offset into the BLOB to start writing.
     * @return SQLITE_OK on success.
     */
    inline int write(const SqliteBufferSlice& slice, int offset) {
        return write(slice.data(), static_cast<int>(slice.bytes()), offset);
    }

    /**
     * @brief Fast-path to redirect this stream to a different row in the same table/column.
     * 
     * @param new_rowid The new row to open.
     * @return SQLITE_OK on success.
     */
    inline int reopen(sqlite3_int64 new_rowid) {
        return m_blob ? sqlite3_blob_reopen(m_blob, new_rowid) : SQLITE_MISUSE;
    }
};

#endif // SQLITE3_BLOB_STREAM_H
