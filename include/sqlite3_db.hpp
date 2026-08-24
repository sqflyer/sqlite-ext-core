#ifndef SQLITE3_DB_HPP
#define SQLITE3_DB_HPP

#include <sqlite3.h>
#include "sqlite3_statement.hpp"

/**
 * @brief Non-owning view of a SQLite Database connection.
 * 
 * Provides modern, type-safe C++ helpers (such as RAII statement preparation and execution)
 * without taking ownership or closing the underlying raw `sqlite3*` handle.
 * Ideal for passing database references into functions or wrapping connections managed elsewhere.
 */
class SqliteDatabaseView {
protected:
    sqlite3* m_db;

public:
    /**
     * @brief Construct a non-owning database view.
     * @param db The raw SQLite database handle (defaults to nullptr).
     */
    inline SqliteDatabaseView(sqlite3* db = nullptr) : m_db(db) {}
    
    /** @brief Retrieves the underlying raw sqlite3* pointer. */
    inline sqlite3* get() const { return m_db; }

    /** @brief Implicit conversion operator to raw sqlite3* pointer for seamless SQLite C-API interoperability. */
    inline operator sqlite3*() const { return m_db; }

    /**
     * @brief Prepares a modern C++ RAII SqliteStatement for execution.
     * @param sql The SQL query string to compile.
     * @return An initialized SqliteStatement instance.
     */
    inline SqliteStatement prepare(const char* sql) const {
        return SqliteStatement(m_db, sql);
    }
    
    /**
     * @brief Executes one or more SQL statements without parameter bindings.
     * 
     * Ideal for DDL (e.g. `CREATE TABLE`, `CREATE INDEX`) or bulk parameterless DML operations.
     * @param sql One or more semicolon-separated SQL statements.
     * @return SQLITE_OK on success, or an SQLite error code.
     */
    inline int exec(const char* sql) const {
        return sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);
    }

    /**
     * @brief Enables or disables runtime extension loading capability on this database handle.
     * @param onoff True to enable extension loading, false to disable.
     * @return SQLITE_OK on success, or an SQLite error code.
     */
    inline int enable_load_extension(bool onoff = true) const {
        return sqlite3_enable_load_extension(m_db, onoff ? 1 : 0);
    }

    /**
     * @brief Dynamically loads a compiled extension library into the SQLite database connection.
     * @param zFile The path to the shared library / DLL file.
     * @param zProc Optional entry point procedure name (nullptr defaults to sqlite3_extension_init).
     * @param pzErrMsg Optional pointer to receive error messages allocated by SQLite.
     * @return SQLITE_OK on success, or an SQLite error code.
     */
    inline int load_extension(const char* zFile, const char* zProc = nullptr, char** pzErrMsg = nullptr) const {
        return sqlite3_load_extension(m_db, zFile, zProc, pzErrMsg);
    }
};

/**
 * @brief Memory-owning RAII wrapper for a SQLite Database connection.
 * 
 * Automatically manages the lifecycle of a SQLite database handle, ensuring `sqlite3_close_v2`
 * is deterministically called when the object goes out of scope. Supports move semantics
 * and explicitly prohibits copying to prevent double-free errors.
 */
class SqliteDatabaseOwned : public SqliteDatabaseView {
public:
    /**
     * @brief Opens a new SQLite database connection with RAII ownership.
     * @param filename The path to the database file or ":memory:".
     * @param flags SQLite open flags (defaults to read-write and create).
     * @param zVfs Optional custom VFS name (defaults to default SQLite VFS).
     */
    inline SqliteDatabaseOwned(const char* filename, int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, const char* zVfs = nullptr) 
        : SqliteDatabaseView(nullptr) 
    {
        sqlite3_open_v2(filename, &m_db, flags, zVfs);
    }

    /**
     * @brief Destructor automatically closes the SQLite database connection using sqlite3_close_v2.
     */
    inline ~SqliteDatabaseOwned() {
        if (m_db) {
            sqlite3_close_v2(m_db);
        }
    }

    // Non-copyable to ensure unique handle ownership
    SqliteDatabaseOwned(const SqliteDatabaseOwned&) = delete;
    SqliteDatabaseOwned& operator=(const SqliteDatabaseOwned&) = delete;

    /**
     * @brief Move constructor transfers database handle ownership.
     */
    inline SqliteDatabaseOwned(SqliteDatabaseOwned&& other) noexcept : SqliteDatabaseView(other.m_db) {
        other.m_db = nullptr;
    }

    /**
     * @brief Move assignment operator safely closes existing handle and transfers new ownership.
     */
    inline SqliteDatabaseOwned& operator=(SqliteDatabaseOwned&& other) noexcept {
        if (this != &other) {
            if (m_db) {
                sqlite3_close_v2(m_db);
            }
            m_db = other.m_db;
            other.m_db = nullptr;
        }
        return *this;
    }
};

#ifndef SQLITE_EXT_STATE_FWD_DECLARED
#define SQLITE_EXT_STATE_FWD_DECLARED
class SqliteRwLock;
template <typename T, typename LockPolicy = SqliteRwLock> class SqliteExtState;
#endif

/**
 * @brief Zero-allocation, lightweight RAII wrapper for sqlite3_context inside UDFs, Aggregates, and Virtual Tables.
 * 
 * DESIGN & STATE INJECTION MODEL:
 * ----------------------------------------------------------------------------
 * SqliteContext is a 16-byte stack structure wrapping SQLite's native `sqlite3_context*`
 * alongside an optional injected `m_user_data` pointer.
 * 
 * 1. Scalar UDFs & Aggregates:
 *    SQLite's C engine automatically routes `pApp` to `sqlite3_user_data(m_ctx)`.
 *    In these contexts, `m_user_data` is nullptr, and `user_data()` falls back to
 *    `sqlite3_user_data(m_ctx)` in 1 CPU instruction.
 * 
 * 2. Virtual Tables & Table-Valued Functions (TVFs):
 *    SQLite's C engine passes an ephemeral context where `sqlite3_user_data(m_ctx)` is NULL.
 *    The TVF / VTab module captures `pAux` (the shared state Entry pointer) during `xConnect`,
 *    and injects it directly into `SqliteContext(ctx, pTab->raw_state)` during `xColumn`.
 * 
 * 3. Unified O(1) State Access:
 *    Calling `ctx.state<T>()` invokes `SqliteExtState<T>::from_context(*this)`, which
 *    accesses `user_data()` and returns `&entry->state` in 1 CPU instruction across
 *    every subsystem without hash table lookups, database searches, or mutex overhead.
 */
class SqliteContext {
private:
    sqlite3_context* m_ctx;       ///< Raw SQLite C-API context handle
    void*            m_user_data;  ///< Injected state / user_data pointer (used for TVF / Virtual Table xColumn)

public:
    /**
     * @brief Constructs a zero-allocation SqliteContext wrapper.
     * @param ctx The underlying raw sqlite3_context pointer from SQLite.
     * @param user_data Optional injected state/aux pointer (e.g. from VTab in xColumn).
     */
    inline explicit SqliteContext(sqlite3_context* ctx, void* user_data = nullptr) 
        : m_ctx(ctx), m_user_data(user_data) {}

    /** @brief Retrieves the raw underlying sqlite3_context pointer. */
    inline sqlite3_context* get() const { return m_ctx; }

    /** @brief Implicit conversion operator to raw sqlite3_context pointer. */
    inline operator sqlite3_context*() const { return m_ctx; }

    /** @brief Retrieves the parent sqlite3* database connection handle associated with this context. */
    inline sqlite3* db_handle() const { return sqlite3_context_db_handle(m_ctx); }

    /**
     * @brief Retrieves the user data / shared state pointer for this execution.
     * Checks the injected `m_user_data` first (TVF/VTab fast-path), and falls back to
     * `sqlite3_user_data(m_ctx)` for Scalar UDFs and Aggregates.
     * @return Raw void* pointer to the bound state Entry, or nullptr.
     */
    inline void* user_data() const { 
        return m_user_data ? m_user_data : (m_ctx ? sqlite3_user_data(m_ctx) : nullptr); 
    }

    /**
     * @brief Retrieves metadata / auxiliary cache data previously saved on this context.
     * @param N The argument index (0-based) to which auxiliary data is bound.
     * @return Cached pointer, or nullptr if none exists.
     */
    inline void* get_auxdata(int N) const { return sqlite3_get_auxdata(m_ctx, N); }

    /**
     * @brief Saves metadata / auxiliary cache data on this context with an automatic cleanup callback.
     * @param N The argument index (0-based) to bind data to.
     * @param data The data pointer to cache.
     * @param free_func Cleanup destructor callback invoked by SQLite when the context is torn down.
     */
    inline void set_auxdata(int N, void* data, void (*free_func)(void*)) { 
        sqlite3_set_auxdata(m_ctx, N, data, free_func); 
    }

    /**
     * @brief Retrieves the strongly-typed shared extension state directly from this context.
     * 
     * Executes in 1 single CPU instruction (O(1)) without lock overhead or dictionary searches.
     * Works uniformly across Scalar UDFs, Aggregates, Table-Valued Functions (TVFs), and Virtual Tables.
     * 
     * @tparam State The user-defined shared state struct type.
     * @return Strongly-typed State* pointer, or nullptr if no state is bound.
     */
    template <typename State>
    inline State* state() const {
        return SqliteExtState<State>::from_context(*this);
    }

    // ========================================================================
    // Result Setting Primitives
    // ========================================================================

    /** @brief Sets a 32-bit signed integer result on the SQLite context. */
    inline void result_int(int iVal) { sqlite3_result_int(m_ctx, iVal); }

    /** @brief Sets a 64-bit signed integer result on the SQLite context. */
    inline void result_int64(sqlite3_int64 iVal) { sqlite3_result_int64(m_ctx, iVal); }

    /** @brief Sets a 64-bit IEEE floating point double result on the SQLite context. */
    inline void result_double(double dVal) { sqlite3_result_double(m_ctx, dVal); }

    /** @brief Sets an SQL NULL result on the SQLite context. */
    inline void result_null() { sqlite3_result_null(m_ctx); }

    // ========================================================================
    // String & Binary Blob Results
    // ========================================================================

    /**
     * @brief Sets a UTF-8 text string result on the SQLite context.
     * @param z Pointer to the null-terminated or sized UTF-8 string buffer.
     * @param n Number of bytes (or -1 to auto-calculate string length via strlen).
     * @param free_func Memory ownership tag (defaults to SQLITE_TRANSIENT to create a deep copy).
     */
    inline void result_text(const char* z, int n = -1, void (*free_func)(void*) = SQLITE_TRANSIENT) {
        sqlite3_result_text(m_ctx, z, n, free_func);
    }

    /**
     * @brief Sets a binary BLOB result on the SQLite context.
     * @param z Pointer to the binary payload data buffer.
     * @param n Number of bytes in the blob buffer.
     * @param free_func Memory ownership tag (defaults to SQLITE_TRANSIENT to create a deep copy).
     */
    inline void result_blob(const void* z, int n, void (*free_func)(void*) = SQLITE_TRANSIENT) {
        sqlite3_result_blob(m_ctx, z, n, free_func);
    }

    /**
     * @brief Sets a zero-filled blob of specified byte length on the SQLite context.
     * @param n The number of zero bytes to allocate.
     */
    inline void result_zeroblob(int n) { sqlite3_result_zeroblob(m_ctx, n); }

    // ========================================================================
    // Error Reporting
    // ========================================================================

    /**
     * @brief Returns a custom SQL error message to the SQLite query engine.
     * @param z The UTF-8 error string description.
     * @param n Length in bytes (or -1 for null-terminated).
     */
    inline void result_error(const char* z, int n = -1) { sqlite3_result_error(m_ctx, z, n); }

    /** @brief Reports a string/blob size overflow error (SQLITE_TOOBIG) to the engine. */
    inline void result_error_toobig() { sqlite3_result_error_toobig(m_ctx); }

    /** @brief Reports an out-of-memory error (SQLITE_NOMEM) to the engine. */
    inline void result_error_nomem() { sqlite3_result_error_nomem(m_ctx); }

    /**
     * @brief Returns a specific SQLite numeric error code to the query engine.
     * @param errCode The SQLite error code (e.g. SQLITE_CONSTRAINT, SQLITE_MISMATCH).
     */
    inline void result_error_code(int errCode) { sqlite3_result_error_code(m_ctx, errCode); }
};

#endif // SQLITE3_DB_HPP
