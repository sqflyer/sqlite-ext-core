#ifndef SQLITE3_STATEMENT_HPP
#define SQLITE3_STATEMENT_HPP

#include <sqlite3.h>
#include "sqlite3_value.hpp"

/**
 * @brief Zero-dependency C++ RAII wrapper over SQLite prepared statements (`sqlite3_stmt*`).
 * 
 * Provides move-only lifetime management, type-safe fluent parameter binding,
 * and zero-allocation column extraction.
 */
class SqliteStatement {
private:
    sqlite3_stmt* m_stmt;

    // Non-copyable
    SqliteStatement(const SqliteStatement&) = delete;
    SqliteStatement& operator=(const SqliteStatement&) = delete;

protected:
    /**
     * @brief Protected constructor for derived classes (e.g. SqliteCachedStatement) 
     * to wrap an existing raw statement handle.
     */
    inline explicit SqliteStatement(sqlite3_stmt* stmt) noexcept : m_stmt(stmt) {}

public:
    /**
     * @brief Constructs an empty, unprepared statement wrapper.
     */
    inline SqliteStatement() noexcept : m_stmt(nullptr) {}

    /**
     * @brief Constructs and prepares a statement immediately.
     * 
     * @param db The SQLite database connection handle.
     * @param sql The SQL statement string (UTF-8).
     * @param nByte Length of sql in bytes, or -1 if null-terminated.
     */
    inline SqliteStatement(sqlite3* db, const char* sql, int nByte = -1) : m_stmt(nullptr) {
        prepare(db, sql, nByte);
    }

    /**
     * @brief Destructor that automatically finalizes the prepared statement.
     */
    inline ~SqliteStatement() {
        finalize();
    }

    /**
     * @brief Move constructor. Transfers ownership of the statement handle.
     * 
     * @param other The rvalue SqliteStatement to move from.
     */
    inline SqliteStatement(SqliteStatement&& other) noexcept : m_stmt(other.m_stmt) {
        other.m_stmt = nullptr;
    }

    /**
     * @brief Move assignment operator. Finalizes existing statement and adopts new handle.
     * 
     * @param other The rvalue SqliteStatement to move from.
     * @return Reference to this statement wrapper.
     */
    inline SqliteStatement& operator=(SqliteStatement&& other) noexcept {
        if (this != &other) {
            finalize();
            m_stmt = other.m_stmt;
            other.m_stmt = nullptr;
        }
        return *this;
    }

    // =========================================================================
    // Lifecycle & Handle Management
    // =========================================================================

    /**
     * @brief Prepares a new SQL statement, finalizing any existing prepared statement.
     * 
     * @param db SQLite database connection handle.
     * @param sql UTF-8 SQL string.
     * @param nByte Length of sql in bytes, or -1 if null-terminated.
     * @return SQLite error code (SQLITE_OK on success).
     */
    inline int prepare(sqlite3* db, const char* sql, int nByte = -1) {
        finalize();
        if (!db || !sql) return SQLITE_MISUSE;
        return sqlite3_prepare_v2(db, sql, nByte, &m_stmt, nullptr);
    }

    /**
     * @brief Explicitly finalizes the statement and sets handle to nullptr.
     * 
     * @return SQLite error code returned by sqlite3_finalize (SQLITE_OK if already null).
     */
    inline int finalize() noexcept {
        if (m_stmt) {
            sqlite3_stmt* stmt = m_stmt;
            m_stmt = nullptr;
            return sqlite3_finalize(stmt);
        }
        return SQLITE_OK;
    }

    /**
     * @brief Returns the raw underlying sqlite3_stmt handle.
     * 
     * @return Raw sqlite3_stmt* pointer or nullptr if unprepared.
     */
    inline sqlite3_stmt* get() const noexcept {
        return m_stmt;
    }

    /**
     * @brief Releases ownership of the sqlite3_stmt handle without finalizing it.
     * 
     * @return The relinquished raw sqlite3_stmt* pointer.
     */
    inline sqlite3_stmt* release() noexcept {
        sqlite3_stmt* stmt = m_stmt;
        m_stmt = nullptr;
        return stmt;
    }

    /**
     * @brief Checks if the statement holds a valid non-null handle.
     * 
     * @return true if prepared and valid, false otherwise.
     */
    inline explicit operator bool() const noexcept {
        return m_stmt != nullptr;
    }

    // =========================================================================
    // Stepping & Execution
    // =========================================================================

    /**
     * @brief Advances the statement by one step.
     * 
     * @return SQLITE_ROW if a new row is available, SQLITE_DONE if finished, or error code.
     */
    inline int step() {
        return m_stmt ? sqlite3_step(m_stmt) : SQLITE_MISUSE;
    }

    /**
     * @brief Convenience method to iterate rows in a while loop.
     * 
     * @return true if a row is available (SQLITE_ROW), false on SQLITE_DONE or error.
     */
    inline bool next() {
        return step() == SQLITE_ROW;
    }

    /**
     * @brief Resets the statement back to its initial state for re-execution.
     * 
     * @return SQLITE_OK on success, or error code.
     */
    inline int reset() {
        return m_stmt ? sqlite3_reset(m_stmt) : SQLITE_MISUSE;
    }

    /**
     * @brief Clears all bound parameters back to SQL NULL.
     * 
     * @return SQLITE_OK on success, or error code.
     */
    inline int clear_bindings() {
        return m_stmt ? sqlite3_clear_bindings(m_stmt) : SQLITE_MISUSE;
    }

    /**
     * @brief Executes the statement to completion and automatically resets it.
     * 
     * Ideal for DDL/DML statements like INSERT, UPDATE, DELETE, CREATE TABLE.
     * 
     * @return SQLITE_DONE on successful execution, or error code.
     */
    inline int execute() {
        if (!m_stmt) return SQLITE_MISUSE;
        int rc = sqlite3_step(m_stmt);
        sqlite3_reset(m_stmt);
        return rc;
    }

    // =========================================================================
    // Parameter Binding (1-based index)
    // =========================================================================

    /**
     * @brief Resolves the 1-based index of a named SQL parameter (e.g., ":id", "@name").
     * 
     * @param name The named parameter string.
     * @return 1-based index, or 0 if not found / statement is null.
     */
    inline int bind_parameter_index(const char* name) const {
        return m_stmt ? sqlite3_bind_parameter_index(m_stmt, name) : 0;
    }

    /**
     * @brief Binds a 32-bit integer to a 1-based parameter index.
     * 
     * @param col 1-based parameter index.
     * @param val 32-bit integer value.
     * @return SQLITE_OK on success, or error code.
     */
    inline int bind(int col, int val) {
        return m_stmt ? sqlite3_bind_int(m_stmt, col, val) : SQLITE_MISUSE;
    }

    /**
     * @brief Binds a 64-bit integer to a 1-based parameter index.
     * 
     * @param col 1-based parameter index.
     * @param val 64-bit integer value.
     * @return SQLITE_OK on success, or error code.
     */
    inline int bind(int col, sqlite3_int64 val) {
        return m_stmt ? sqlite3_bind_int64(m_stmt, col, val) : SQLITE_MISUSE;
    }

    /**
     * @brief Binds a 64-bit floating-point number to a 1-based parameter index.
     * 
     * @param col 1-based parameter index.
     * @param val Double-precision floating-point value.
     * @return SQLITE_OK on success, or error code.
     */
    inline int bind(int col, double val) {
        return m_stmt ? sqlite3_bind_double(m_stmt, col, val) : SQLITE_MISUSE;
    }

    /**
     * @brief Binds SQL NULL to a 1-based parameter index.
     * 
     * @param col 1-based parameter index.
     * @return SQLITE_OK on success, or error code.
     */
    inline int bind_null(int col) {
        return m_stmt ? sqlite3_bind_null(m_stmt, col) : SQLITE_MISUSE;
    }

    /**
     * @brief Binds a UTF-8 text string to a 1-based parameter index.
     * 
     * @param col 1-based parameter index.
     * @param text UTF-8 string pointer.
     * @param len Length in bytes (-1 if null-terminated).
     * @param dtor Destructor callback (default SQLITE_TRANSIENT).
     * @return SQLITE_OK on success, or error code.
     */
    inline int bind(int col, const char* text, int len = -1, void (*dtor)(void*) = SQLITE_TRANSIENT) {
        return m_stmt ? sqlite3_bind_text(m_stmt, col, text, len, dtor) : SQLITE_MISUSE;
    }

    /**
     * @brief Binds a SqliteStringView wrapper to a 1-based parameter index.
     * 
     * @param col 1-based parameter index.
     * @param str SqliteStringView instance.
     * @param dtor Destructor callback (default SQLITE_TRANSIENT).
     * @return SQLITE_OK on success, or error code.
     */
    inline int bind(int col, const SqliteStringView& str, void (*dtor)(void*) = SQLITE_TRANSIENT) {
        return m_stmt ? str.bind(m_stmt, col, dtor) : SQLITE_MISUSE;
    }

    /**
     * @brief Binds a memory-managed SqliteStringOwned wrapper to a 1-based parameter index.
     * 
     * @param col 1-based parameter index.
     * @param str SqliteStringOwned instance.
     * @return SQLITE_OK on success, or error code.
     */
    inline int bind(int col, const SqliteStringOwned& str) {
        return m_stmt ? str.bind(m_stmt, col) : SQLITE_MISUSE;
    }

    /**
     * @brief Binds binary blob memory to a 1-based parameter index.
     * 
     * @param col 1-based parameter index.
     * @param blob Pointer to binary buffer.
     * @param len Length in bytes.
     * @param dtor Destructor callback (default SQLITE_TRANSIENT).
     * @return SQLITE_OK on success, or error code.
     */
    inline int bind(int col, const void* blob, int len, void (*dtor)(void*) = SQLITE_TRANSIENT) {
        return m_stmt ? sqlite3_bind_blob(m_stmt, col, blob, len, dtor) : SQLITE_MISUSE;
    }

    /**
     * @brief Binds a SqliteBlobView wrapper to a 1-based parameter index.
     * 
     * @param col 1-based parameter index.
     * @param blob SqliteBlobView instance.
     * @param dtor Destructor callback (default SQLITE_TRANSIENT).
     * @return SQLITE_OK on success, or error code.
     */
    inline int bind(int col, const SqliteBlobView& blob, void (*dtor)(void*) = SQLITE_TRANSIENT) {
        return m_stmt ? blob.bind(m_stmt, col, dtor) : SQLITE_MISUSE;
    }

    /**
     * @brief Binds a memory-managed SqliteBlobOwned wrapper to a 1-based parameter index.
     * 
     * @param col 1-based parameter index.
     * @param blob SqliteBlobOwned instance.
     * @return SQLITE_OK on success, or error code.
     */
    inline int bind(int col, const SqliteBlobOwned& blob) {
        return m_stmt ? blob.bind(m_stmt, col) : SQLITE_MISUSE;
    }

    /**
     * @brief Binds a transient polymorphic SqliteValueView wrapper to a 1-based parameter index.
     * 
     * @param col 1-based parameter index.
     * @param val SqliteValueView instance.
     * @return SQLITE_OK on success, or error code.
     */
    inline int bind(int col, const SqliteValueView& val) {
        return m_stmt ? val.bind(m_stmt, col) : SQLITE_MISUSE;
    }

    /**
     * @brief Binds an owned polymorphic SqliteValueOwned wrapper to a 1-based parameter index.
     * 
     * @param col 1-based parameter index.
     * @param val SqliteValueOwned instance.
     * @return SQLITE_OK on success, or error code.
     */
    inline int bind(int col, const SqliteValueOwned& val) {
        return m_stmt ? val.bind(m_stmt, col) : SQLITE_MISUSE;
    }

    // =========================================================================
    // Named Parameter Binding Overloads
    // =========================================================================

    /**
     * @brief Generic binding overload using a named parameter (e.g. ":id", "@score").
     * 
     * @tparam T Type of value to bind.
     * @param name The parameter name string.
     * @param val Value to bind.
     * @return SQLITE_OK on success, SQLITE_NOTFOUND if name is invalid, or error code.
     */
    template<typename T>
    inline int bind(const char* name, const T& val) {
        int idx = bind_parameter_index(name);
        if (idx == 0) return SQLITE_NOTFOUND;
        return bind(idx, val);
    }

    /**
     * @brief Binds a text string using a named parameter (e.g. ":name").
     * 
     * @param name The parameter name string.
     * @param text UTF-8 string pointer.
     * @param len Length in bytes (-1 if null-terminated).
     * @param dtor Destructor callback (default SQLITE_TRANSIENT).
     * @return SQLITE_OK on success, SQLITE_NOTFOUND if name is invalid, or error code.
     */
    inline int bind(const char* name, const char* text, int len = -1, void (*dtor)(void*) = SQLITE_TRANSIENT) {
        int idx = bind_parameter_index(name);
        if (idx == 0) return SQLITE_NOTFOUND;
        return bind(idx, text, len, dtor);
    }

    /**
     * @brief Binds a binary blob using a named parameter (e.g. ":avatar").
     * 
     * @param name The parameter name string.
     * @param blob Pointer to binary buffer.
     * @param len Length in bytes.
     * @param dtor Destructor callback (default SQLITE_TRANSIENT).
     * @return SQLITE_OK on success, SQLITE_NOTFOUND if name is invalid, or error code.
     */
    inline int bind(const char* name, const void* blob, int len, void (*dtor)(void*) = SQLITE_TRANSIENT) {
        int idx = bind_parameter_index(name);
        if (idx == 0) return SQLITE_NOTFOUND;
        return bind(idx, blob, len, dtor);
    }

    /**
     * @brief Binds SQL NULL using a named parameter (e.g. ":optional_val").
     * 
     * @param name The parameter name string.
     * @return SQLITE_OK on success, SQLITE_NOTFOUND if name is invalid, or error code.
     */
    inline int bind_null(const char* name) {
        int idx = bind_parameter_index(name);
        if (idx == 0) return SQLITE_NOTFOUND;
        return bind_null(idx);
    }

    // =========================================================================
    // Column Data Extraction (0-based index)
    // =========================================================================

    /**
     * @brief Returns the number of columns in the result set.
     * 
     * @return Number of columns (0 if unprepared).
     */
    inline int column_count() const {
        return m_stmt ? sqlite3_column_count(m_stmt) : 0;
    }

    /**
     * @brief Returns the SQLite datatype code of a column (0-based).
     * 
     * @param col 0-based column index.
     * @return SQLITE_INTEGER, SQLITE_FLOAT, SQLITE_TEXT, SQLITE_BLOB, or SQLITE_NULL.
     */
    inline int column_type(int col) const {
        return m_stmt ? sqlite3_column_type(m_stmt, col) : SQLITE_NULL;
    }

    /**
     * @brief Returns the name of a column in the result set (0-based).
     * 
     * @param col 0-based column index.
     * @return Column name string, or nullptr if unprepared / invalid index.
     */
    inline const char* column_name(int col) const {
        return m_stmt ? sqlite3_column_name(m_stmt, col) : nullptr;
    }

    /**
     * @brief Returns the column value as a 32-bit integer (0-based).
     * 
     * @param col 0-based column index.
     * @return 32-bit integer value.
     */
    inline int column_int(int col) const {
        return m_stmt ? sqlite3_column_int(m_stmt, col) : 0;
    }

    /**
     * @brief Returns the column value as a 64-bit integer (0-based).
     * 
     * @param col 0-based column index.
     * @return 64-bit integer value.
     */
    inline sqlite3_int64 column_int64(int col) const {
        return m_stmt ? sqlite3_column_int64(m_stmt, col) : 0;
    }

    /**
     * @brief Returns the column value as a 64-bit floating-point number (0-based).
     * 
     * @param col 0-based column index.
     * @return Double-precision float value.
     */
    inline double column_double(int col) const {
        return m_stmt ? sqlite3_column_double(m_stmt, col) : 0.0;
    }

    /**
     * @brief Returns the column value as a UTF-8 text pointer (0-based).
     * 
     * @param col 0-based column index.
     * @return UTF-8 text string pointer.
     */
    inline const char* column_text(int col) const {
        return m_stmt ? reinterpret_cast<const char*>(sqlite3_column_text(m_stmt, col)) : nullptr;
    }

    /**
     * @brief Returns the number of bytes in a text or blob column (0-based).
     * 
     * @param col 0-based column index.
     * @return Byte count.
     */
    inline int column_bytes(int col) const {
        return m_stmt ? sqlite3_column_bytes(m_stmt, col) : 0;
    }

    /**
     * @brief Returns the column value as a binary blob pointer (0-based).
     * 
     * @param col 0-based column index.
     * @return Pointer to binary data buffer.
     */
    inline const void* column_blob(int col) const {
        return m_stmt ? sqlite3_column_blob(m_stmt, col) : nullptr;
    }

    /**
     * @brief Returns the internal sqlite3_value pointer for a column (0-based).
     * 
     * @param col 0-based column index.
     * @return Raw sqlite3_value* pointer.
     */
    inline sqlite3_value* column_value(int col) const {
        return m_stmt ? sqlite3_column_value(m_stmt, col) : nullptr;
    }

    // =========================================================================
    // Zero-Allocation View & Owned Wrapper Extractions
    // =========================================================================

    /**
     * @brief Returns a zero-allocation SqliteStringView for the column text (0-based).
     * 
     * @param col 0-based column index.
     * @return SqliteStringView wrapping internal SQLite memory buffer.
     */
    inline SqliteStringView column_string_view(int col) const {
        if (!m_stmt) return SqliteStringView(nullptr, 0);
        const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(m_stmt, col));
        int bytes = sqlite3_column_bytes(m_stmt, col);
        return SqliteStringView(txt, bytes);
    }

    /**
     * @brief Returns a zero-allocation SqliteBlobView for the column blob (0-based).
     * 
     * @param col 0-based column index.
     * @return SqliteBlobView wrapping internal SQLite memory buffer.
     */
    inline SqliteBlobView column_blob_view(int col) const {
        if (!m_stmt) return SqliteBlobView(nullptr, 0);
        const void* data = sqlite3_column_blob(m_stmt, col);
        int bytes = sqlite3_column_bytes(m_stmt, col);
        return SqliteBlobView(data, bytes);
    }

    /**
     * @brief Returns a zero-allocation transient SqliteValueView for the column (0-based).
     * 
     * @param col 0-based column index.
     * @return SqliteValueView wrapping the column's sqlite3_value*.
     */
    inline SqliteValueView column_value_view(int col) const {
        return SqliteValueView(column_value(col));
    }

    /**
     * @brief Returns a memory-managed SqliteValueOwned copy of the column value (0-based).
     * 
     * @param col 0-based column index.
     * @return SqliteValueOwned instance with deep copied or SBO storage.
     */
    inline SqliteValueOwned column_value_owned(int col) const {
        return SqliteValueOwned(column_value(col));
    }
};

// ============================================================================
// CACHED STATEMENT WRAPPER
// ============================================================================

/**
 * @brief RAII wrapper for a borrowed, cached sqlite3_stmt.
 * 
 * Unlike SqliteStatement, this does NOT finalize the statement on destruction.
 * Instead, it calls sqlite3_clear_bindings() and sqlite3_reset(), returning it
 * to a clean state for the next user of the cache.
 * 
 * WARNING: This wrapper (and the underlying sqlite3_stmt handle) is NOT thread-safe
 * and cannot be safely used for parallel reads or recursive execution. 
 * It should be exclusively used in tight loops (e.g. executing queries inside a UDF) 
 * for maximum performance where preparing a statement repeatedly would be a bottleneck.
 */
class SqliteCachedStatement : public SqliteStatement {
public:
    /**
     * @brief Constructs a cached statement wrapper from a raw handle.
     * @param stmt A prepared statement owned by a cache.
     */
    explicit SqliteCachedStatement(sqlite3_stmt* stmt = nullptr) noexcept 
        : SqliteStatement(stmt) {}

    // Delete copy semantics
    SqliteCachedStatement(const SqliteCachedStatement&) = delete;
    SqliteCachedStatement& operator=(const SqliteCachedStatement&) = delete;

    // Move semantics
    inline SqliteCachedStatement(SqliteCachedStatement&& other) noexcept 
        : SqliteStatement(static_cast<SqliteStatement&&>(other)) {}

    inline SqliteCachedStatement& operator=(SqliteCachedStatement&& other) noexcept {
        if (this != &other) {
            release_to_cache();
            SqliteStatement::operator=(static_cast<SqliteStatement&&>(other));
        }
        return *this;
    }

    /**
     * @brief Destructor. Resets and clears bindings instead of finalizing.
     */
    ~SqliteCachedStatement() {
        release_to_cache();
    }

private:
    inline void release_to_cache() noexcept {
        sqlite3_stmt* stmt = release(); // Base release() sets m_stmt = nullptr
        if (stmt) {
            sqlite3_clear_bindings(stmt);
            sqlite3_reset(stmt);
        }
    }
};

#endif // SQLITE3_STATEMENT_HPP
