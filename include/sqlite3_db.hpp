#ifndef SQLITE3_DB_HPP
#define SQLITE3_DB_HPP

#include "sqlite3ext.h"
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

#endif // SQLITE3_DB_HPP
