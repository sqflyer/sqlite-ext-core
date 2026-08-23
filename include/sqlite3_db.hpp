#ifndef SQLITE3_DB_HPP
#define SQLITE3_DB_HPP

#include <sqlite3.h>
#include "sqlite3_statement.hpp"

/**
 * @brief Non-owning view of a SQLite Database connection.
 * Provides modern C++ helpers (like RAII statement preparation) without taking ownership of the handle.
 */
class SqliteDatabaseView {
protected:
    sqlite3* m_db;

public:
    inline SqliteDatabaseView(sqlite3* db = nullptr) : m_db(db) {}
    
    inline sqlite3* get() const { return m_db; }
    inline operator sqlite3*() const { return m_db; }

    /**
     * @brief Prepares a modern C++ RAII SqliteStatement.
     */
    inline SqliteStatement prepare(const char* sql) const {
        return SqliteStatement(m_db, sql);
    }
    
    /**
     * @brief Executes one or more SQL statements without bindings.
     * Perfect for DDL (CREATE TABLE) or bulk parameterless INSERTS.
     */
    inline int exec(const char* sql) const {
        return sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);
    }

    /**
     * @brief Enables or disables the extension loading capability.
     */
    inline int enable_load_extension(bool onoff = true) const {
        return sqlite3_enable_load_extension(m_db, onoff ? 1 : 0);
    }

    /**
     * @brief Dynamically loads an extension library into the SQLite database connection.
     */
    inline int load_extension(const char* zFile, const char* zProc = nullptr, char** pzErrMsg = nullptr) const {
        return sqlite3_load_extension(m_db, zFile, zProc, pzErrMsg);
    }
};

/**
 * @brief Memory-owning RAII wrapper for a SQLite Database connection.
 * Automatically closes the database handle when it goes out of scope.
 */
class SqliteDatabaseOwned : public SqliteDatabaseView {
public:
    /**
     * @brief Opens a new database connection.
     */
    inline SqliteDatabaseOwned(const char* filename, int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, const char* zVfs = nullptr) 
        : SqliteDatabaseView(nullptr) 
    {
        sqlite3_open_v2(filename, &m_db, flags, zVfs);
    }

    /**
     * @brief Automatically closes the database connection.
     */
    inline ~SqliteDatabaseOwned() {
        if (m_db) {
            sqlite3_close_v2(m_db);
        }
    }

    // Non-copyable
    SqliteDatabaseOwned(const SqliteDatabaseOwned&) = delete;
    SqliteDatabaseOwned& operator=(const SqliteDatabaseOwned&) = delete;

    // Moveable
    inline SqliteDatabaseOwned(SqliteDatabaseOwned&& other) noexcept : SqliteDatabaseView(other.m_db) {
        other.m_db = nullptr;
    }

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

template <typename T> class SqliteExtState;

/**
 * @brief Zero-allocation wrapper for sqlite3_context inside UDFs and Virtual Tables.
 * Simplifies setting results and throwing errors.
 */
class SqliteContext {
private:
    sqlite3_context* m_ctx;

public:
    inline explicit SqliteContext(sqlite3_context* ctx) : m_ctx(ctx) {}

    // Expose the raw pointer if needed
    inline sqlite3_context* get() const { return m_ctx; }
    inline operator sqlite3_context*() const { return m_ctx; }
    inline sqlite3* db_handle() const { return sqlite3_context_db_handle(m_ctx); }

    // User Data & Aux Data
    inline void* user_data() const { return sqlite3_user_data(m_ctx); }
    inline void* get_auxdata(int N) const { return sqlite3_get_auxdata(m_ctx, N); }
    inline void set_auxdata(int N, void* data, void (*free_func)(void*)) { sqlite3_set_auxdata(m_ctx, N, data, free_func); }

    /**
     * @brief Retrieve extension shared state directly from this context.
     * @tparam State The user-defined state struct.
     * @return Strongly-typed State* pointer.
     */
    template <typename State>
    inline State* state() const {
        return SqliteExtState<State>::from_context(m_ctx);
    }

    // Fast primitives
    inline void result_int(int iVal) { sqlite3_result_int(m_ctx, iVal); }
    inline void result_int64(sqlite3_int64 iVal) { sqlite3_result_int64(m_ctx, iVal); }
    inline void result_double(double dVal) { sqlite3_result_double(m_ctx, dVal); }
    inline void result_null() { sqlite3_result_null(m_ctx); }

    // String / Blob with memory ownership tags
    inline void result_text(const char* z, int n = -1, void (*free_func)(void*) = SQLITE_TRANSIENT) {
        sqlite3_result_text(m_ctx, z, n, free_func);
    }
    inline void result_blob(const void* z, int n, void (*free_func)(void*) = SQLITE_TRANSIENT) {
        sqlite3_result_blob(m_ctx, z, n, free_func);
    }
    inline void result_zeroblob(int n) { sqlite3_result_zeroblob(m_ctx, n); }

    // Errors
    inline void result_error(const char* z, int n = -1) { sqlite3_result_error(m_ctx, z, n); }
    inline void result_error_toobig() { sqlite3_result_error_toobig(m_ctx); }
    inline void result_error_nomem() { sqlite3_result_error_nomem(m_ctx); }
    inline void result_error_code(int errCode) { sqlite3_result_error_code(m_ctx, errCode); }
};

#endif // SQLITE3_DB_HPP
