#ifndef SQLITE3_TRANSACTION_HPP
#define SQLITE3_TRANSACTION_HPP

#include "sqlite3ext.h"
#include "sqlite3_db.hpp"

enum class SqliteTransactionBehavior {
    DEFERRED,
    IMMEDIATE,
    EXCLUSIVE
};

/**
 * @brief Exception-safe RAII wrapper for SQLite Transactions (BEGIN / COMMIT / ROLLBACK).
 * 
 * Automatically rolls back the transaction if it goes out of scope without being explicitly
 * committed or rolled back. Perfect for guarding against database locks on early returns.
 */
class SqliteTransaction {
    sqlite3* m_db;
    bool m_active;

    // Non-copyable
    SqliteTransaction(const SqliteTransaction&) = delete;
    SqliteTransaction& operator=(const SqliteTransaction&) = delete;

public:
    /**
     * @brief Begins a new transaction.
     * @param db SQLite database connection wrapper (View or Owned).
     * @param behavior Transaction type (DEFERRED, IMMEDIATE, EXCLUSIVE).
     */
    inline SqliteTransaction(SqliteDatabaseView db, SqliteTransactionBehavior behavior = SqliteTransactionBehavior::DEFERRED) 
        : m_db(db.get()), m_active(false) 
    {
        if (!m_db) return;

        const char* sql = "BEGIN DEFERRED;";
        if (behavior == SqliteTransactionBehavior::IMMEDIATE) {
            sql = "BEGIN IMMEDIATE;";
        } else if (behavior == SqliteTransactionBehavior::EXCLUSIVE) {
            sql = "BEGIN EXCLUSIVE;";
        }
        
        if (sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr) == SQLITE_OK) {
            m_active = true;
        }
    }

    /**
     * @brief Explicitly commits the transaction.
     * @return SQLite error code (SQLITE_OK on success).
     */
    inline int commit() noexcept {
        if (!m_active) return SQLITE_MISUSE;
        
        int rc = sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
        if (rc == SQLITE_OK) {
            m_active = false;
        }
        return rc;
    }

    /**
     * @brief Explicitly rolls back the transaction.
     * @return SQLite error code (SQLITE_OK on success).
     */
    inline int rollback() noexcept {
        if (!m_active) return SQLITE_MISUSE;
        
        int rc = sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        if (rc == SQLITE_OK) {
            m_active = false;
        }
        return rc;
    }

    /**
     * @brief Destructor. Automatically rolls back if the transaction is still active.
     */
    inline ~SqliteTransaction() {
        if (m_active) {
            sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        }
    }

    /** @brief Returns true if the transaction is currently active. */
    inline explicit operator bool() const {
        return m_active;
    }

    /** @brief Prepares a modern C++ RAII SqliteStatement within this transaction. */
    inline SqliteStatement prepare(const char* sql) const {
        return SqliteStatement(m_db, sql);
    }
    
    /** @brief Executes one or more SQL statements without bindings within this transaction. */
    inline int exec(const char* sql) const {
        return sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);
    }
};

/**
 * @brief Exception-safe RAII wrapper for nested SQLite Savepoints.
 * 
 * Supports nested transactions via SAVEPOINT / RELEASE / ROLLBACK TO.
 */
class SqliteSavepoint {
    sqlite3* m_db;
    const char* m_name;
    bool m_active;

    // Non-copyable
    SqliteSavepoint(const SqliteSavepoint&) = delete;
    SqliteSavepoint& operator=(const SqliteSavepoint&) = delete;

public:
    /**
     * @brief Creates a new Savepoint.
     * @param db SQLite database connection wrapper (View or Owned).
     * @param name The savepoint identifier.
     */
    inline SqliteSavepoint(SqliteDatabaseView db, const char* name) 
        : m_db(db.get()), m_name(name), m_active(false) 
    {
        if (!m_db || !m_name) return;

        // Uses sqlite3_mprintf with "%w" to securely escape double quotes in identifiers
        char* sql = sqlite3_mprintf("SAVEPOINT \"%w\";", m_name);
        if (sql) {
            if (sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr) == SQLITE_OK) {
                m_active = true;
            }
            sqlite3_free(sql);
        }
    }

    /**
     * @brief Explicitly releases (commits) the savepoint.
     * @return SQLite error code.
     */
    inline int release() noexcept {
        if (!m_active) return SQLITE_MISUSE;
        
        int rc = SQLITE_NOMEM;
        char* sql = sqlite3_mprintf("RELEASE \"%w\";", m_name);
        if (sql) {
            rc = sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);
            if (rc == SQLITE_OK) {
                m_active = false;
            }
            sqlite3_free(sql);
        }
        return rc;
    }

    /**
     * @brief Explicitly rolls back to the savepoint.
     * @return SQLite error code.
     */
    inline int rollback() noexcept {
        if (!m_active) return SQLITE_MISUSE;
        
        int rc = SQLITE_NOMEM;
        char* sql = sqlite3_mprintf("ROLLBACK TO \"%w\";", m_name);
        if (sql) {
            rc = sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);
            if (rc == SQLITE_OK) {
                m_active = false;
            }
            sqlite3_free(sql);
        }
        return rc;
    }

    /**
     * @brief Destructor. Automatically rolls back if the savepoint is still active.
     */
    inline ~SqliteSavepoint() {
        if (m_active) {
            char* sql = sqlite3_mprintf("ROLLBACK TO \"%w\";", m_name);
            if (sql) {
                sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);
                sqlite3_free(sql);
            }
        }
    }

    /** @brief Returns true if the savepoint is currently active. */
    inline explicit operator bool() const {
        return m_active;
    }
};

#endif // SQLITE3_TRANSACTION_HPP
