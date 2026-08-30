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

    // ========================================================================
    // Connection Hooks & Event Handlers (Templatized & Strongly Typed)
    // ========================================================================

    using UpdateHookCallback = void (*)(void* user_data, int operation, const char* db_name, const char* table_name, sqlite3_int64 rowid);
    using CommitHookCallback = int (*)(void* user_data);
    using RollbackHookCallback = void (*)(void* user_data);
    using WalHookCallback = int (*)(void* user_data, sqlite3* db, const char* db_name, int num_pages);
    using ProgressCallback = int (*)(void* user_data);

    /**
     * @brief Registers an update hook with a strongly-typed UserData pointer.
     */
    template <typename UserData>
    inline void* set_update_hook(void (*cb)(UserData* user_data, int operation, const char* db_name, const char* table_name, sqlite3_int64 rowid), UserData* user_data = nullptr) const {
        typedef void (*RawUpdateCb)(void*, int, const char*, const char*, sqlite3_int64);
        return sqlite3_update_hook(m_db, reinterpret_cast<RawUpdateCb>(cb), static_cast<void*>(user_data));
    }

    /**
     * @brief Registers an update hook using a zero-overhead compile-time function template.
     */
    template <void (*Func)(int operation, const char* db_name, const char* table_name, sqlite3_int64 rowid)>
    inline void* set_update_hook() const {
        struct Trampoline {
            static void callback(void*, int op, const char* db, const char* tbl, sqlite3_int64 rowid) {
                Func(op, db, tbl, rowid);
            }
        };
        return sqlite3_update_hook(m_db, Trampoline::callback, nullptr);
    }

    /**
     * @brief Registers a raw void* update hook callback.
     */
    inline void* set_update_hook(UpdateHookCallback cb, void* user_data = nullptr) const {
        return sqlite3_update_hook(m_db, cb, user_data);
    }

    /**
     * @brief Registers a commit hook with a strongly-typed UserData pointer.
     */
    template <typename UserData>
    inline void* set_commit_hook(int (*cb)(UserData* user_data), UserData* user_data = nullptr) const {
        typedef int (*RawCommitCb)(void*);
        return sqlite3_commit_hook(m_db, reinterpret_cast<RawCommitCb>(cb), static_cast<void*>(user_data));
    }

    /**
     * @brief Registers a commit hook using a zero-overhead compile-time function template.
     */
    template <int (*Func)()>
    inline void* set_commit_hook() const {
        struct Trampoline {
            static int callback(void*) { return Func(); }
        };
        return sqlite3_commit_hook(m_db, Trampoline::callback, nullptr);
    }

    /**
     * @brief Registers a raw void* commit hook callback.
     */
    inline void* set_commit_hook(CommitHookCallback cb, void* user_data = nullptr) const {
        return sqlite3_commit_hook(m_db, cb, user_data);
    }

    /**
     * @brief Registers a rollback hook with a strongly-typed UserData pointer.
     */
    template <typename UserData>
    inline void* set_rollback_hook(void (*cb)(UserData* user_data), UserData* user_data = nullptr) const {
        typedef void (*RawRollbackCb)(void*);
        return sqlite3_rollback_hook(m_db, reinterpret_cast<RawRollbackCb>(cb), static_cast<void*>(user_data));
    }

    /**
     * @brief Registers a rollback hook using a zero-overhead compile-time function template.
     */
    template <void (*Func)()>
    inline void* set_rollback_hook() const {
        struct Trampoline {
            static void callback(void*) { Func(); }
        };
        return sqlite3_rollback_hook(m_db, Trampoline::callback, nullptr);
    }

    /**
     * @brief Registers a raw void* rollback hook callback.
     */
    inline void* set_rollback_hook(RollbackHookCallback cb, void* user_data = nullptr) const {
        return sqlite3_rollback_hook(m_db, cb, user_data);
    }

    /**
     * @brief Registers a WAL hook with a strongly-typed UserData pointer.
     */
    template <typename UserData>
    inline void* set_wal_hook(int (*cb)(UserData* user_data, sqlite3* db, const char* db_name, int num_pages), UserData* user_data = nullptr) const {
        typedef int (*RawWalCb)(void*, sqlite3*, const char*, int);
        return sqlite3_wal_hook(m_db, reinterpret_cast<RawWalCb>(cb), static_cast<void*>(user_data));
    }

    /**
     * @brief Registers a WAL hook using a zero-overhead compile-time function template.
     */
    template <int (*Func)(sqlite3* db, const char* db_name, int num_pages)>
    inline void* set_wal_hook() const {
        struct Trampoline {
            static int callback(void*, sqlite3* db, const char* db_name, int pages) {
                return Func(db, db_name, pages);
            }
        };
        return sqlite3_wal_hook(m_db, Trampoline::callback, nullptr);
    }

    /**
     * @brief Registers a raw void* WAL hook callback.
     */
    inline void* set_wal_hook(WalHookCallback cb, void* user_data = nullptr) const {
        return sqlite3_wal_hook(m_db, cb, user_data);
    }

    /**
     * @brief Registers a progress handler with a strongly-typed UserData pointer.
     */
    template <typename UserData>
    inline void set_progress_handler(int num_ops, int (*cb)(UserData* user_data), UserData* user_data = nullptr) const {
        typedef int (*RawProgressCb)(void*);
        sqlite3_progress_handler(m_db, num_ops, reinterpret_cast<RawProgressCb>(cb), static_cast<void*>(user_data));
    }

    /**
     * @brief Registers a progress handler using a zero-overhead compile-time function template.
     */
    template <int (*Func)()>
    inline void set_progress_handler(int num_ops) const {
        struct Trampoline {
            static int callback(void*) { return Func(); }
        };
        sqlite3_progress_handler(m_db, num_ops, Trampoline::callback, nullptr);
    }

    /**
     * @brief Registers a raw void* progress handler callback.
     */
    inline void set_progress_handler(int num_ops, ProgressCallback cb, void* user_data = nullptr) const {
        sqlite3_progress_handler(m_db, num_ops, cb, user_data);
    }

    /**
     * @brief Sets a busy handler timeout in milliseconds.
     * @param ms Timeout in milliseconds.
     * @return SQLITE_OK on success.
     */
    inline int busy_timeout(int ms) const {
        return sqlite3_busy_timeout(m_db, ms);
    }

    /**
     * @brief Interrupts any active operation on this database connection.
     */
    inline void interrupt() const {
        sqlite3_interrupt(m_db);
    }

    /**
     * @brief Retrieves the numeric error code of the most recent failed SQLite operation.
     */
    inline int errcode() const {
        return sqlite3_errcode(m_db);
    }

    /**
     * @brief Retrieves the extended error code of the most recent failed SQLite operation.
     */
    inline int extended_errcode() const {
        return sqlite3_extended_errcode(m_db);
    }

    /**
     * @brief Retrieves the English-language error message describing the most recent failure.
     */
    inline const char* errmsg() const {
        return sqlite3_errmsg(m_db);
    }

    /**
     * @brief Formats an SQLite result code into an English text description.
     */
    static inline const char* errstr(int rc) {
        return sqlite3_errstr(rc);
    }

    /**
     * @brief Returns true if the connection is in autocommit mode (i.e. not inside a transaction).
     */
    inline bool is_autocommit() const {
        return sqlite3_get_autocommit(m_db) != 0;
    }

    /**
     * @brief Checks if a database schema (e.g. "main" or an attached DB) is read-only.
     * @param zDbName Database name (defaults to "main").
     * @return True if read-only, false if writable or unknown.
     */
    inline bool is_readonly(const char* zDbName = "main") const {
        return sqlite3_db_readonly(m_db, zDbName) == 1;
    }

    /**
     * @brief Performs a WAL checkpoint on the specified database.
     * @param zDb Database name (defaults to "main").
     * @param eMode Checkpoint mode (SQLITE_CHECKPOINT_PASSIVE, FULL, RESTART, TRUNCATE).
     * @param pnLog Pointer to receive total number of frames in WAL (optional).
     * @param pnCkpt Pointer to receive total number of check-pointed frames (optional).
     * @return SQLITE_OK on success, or an SQLite error code.
     */
    inline int wal_checkpoint(const char* zDb = "main", int eMode = SQLITE_CHECKPOINT_PASSIVE, int* pnLog = nullptr, int* pnCkpt = nullptr) const {
        return sqlite3_wal_checkpoint_v2(m_db, zDb, eMode, pnLog, pnCkpt);
    }

    /**
     * @brief Retrieves the rowid of the most recent successful INSERT.
     */
    inline sqlite3_int64 last_insert_rowid() const {
        return sqlite3_last_insert_rowid(m_db);
    }

    /**
     * @brief Returns the number of rows modified by the most recent statement.
     */
    inline sqlite3_int64 changes() const {
        return sqlite3_changes64(m_db);
    }

    /**
     * @brief Returns the total number of rows modified since the database was opened.
     */
    inline sqlite3_int64 total_changes() const {
        return sqlite3_total_changes64(m_db);
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
     * @brief Sets a UTF-8 text string result from a SqliteStringView.
     * @param str The SqliteStringView to set as result.
     * @param free_func Memory ownership tag (defaults to SQLITE_TRANSIENT).
     */
    inline void result_text(SqliteStringView str, void (*free_func)(void*) = SQLITE_TRANSIENT) {
        sqlite3_result_text(m_ctx, str.data(), str.length(), free_func);
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
