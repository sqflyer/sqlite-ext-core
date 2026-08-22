#ifndef SQLITE3_BACKUP_HPP
#define SQLITE3_BACKUP_HPP

#include <sqlite3.h>

/**
 * @brief Exception-safe, zero-allocation RAII wrapper for sqlite3_backup.
 * 
 * Ensures that `sqlite3_backup_finish` is always called, preventing the source 
 * database from remaining read-locked if an early return occurs during the backup process.
 */
class SqliteBackup {
private:
    sqlite3_backup* m_backup;

public:
    /** @brief Constructs an empty (uninitialized) backup wrapper. */
    inline SqliteBackup() : m_backup(nullptr) {}

    /**
     * @brief Initialize the backup process.
     * 
     * @param dest_db The destination database connection.
     * @param dest_name The name of the destination database (usually "main").
     * @param src_db The source database connection.
     * @param src_name The name of the source database (usually "main").
     */
    inline SqliteBackup(sqlite3* dest_db, const char* dest_name, 
                        sqlite3* src_db, const char* src_name) {
        m_backup = sqlite3_backup_init(dest_db, dest_name, src_db, src_name);
    }

    // Disable copy semantics to prevent double-free
    SqliteBackup(const SqliteBackup&) = delete;
    SqliteBackup& operator=(const SqliteBackup&) = delete;

    /** @brief Move construct a SqliteBackup, taking ownership of the handle. */
    inline SqliteBackup(SqliteBackup&& other) noexcept : m_backup(other.m_backup) {
        other.m_backup = nullptr;
    }

    /** @brief Move assign a SqliteBackup, destroying the current handle if necessary. */
    inline SqliteBackup& operator=(SqliteBackup&& other) noexcept {
        if (this != &other) {
            finish();
            m_backup = other.m_backup;
            other.m_backup = nullptr;
        }
        return *this;
    }

    /**
     * @brief Destructor guarantees `sqlite3_backup_finish` is called.
     */
    inline ~SqliteBackup() {
        finish();
    }

    /**
     * @brief Returns true if the backup was successfully initialized.
     */
    inline explicit operator bool() const {
        return m_backup != nullptr;
    }

    /**
     * @brief Copy n pages from the source to the destination database.
     * 
     * @param pages The number of pages to copy. If -1, all remaining pages are copied.
     * @return SQLITE_OK on success, SQLITE_DONE if the backup is complete, or an error code.
     */
    inline int step(int pages) {
        if (!m_backup) return SQLITE_MISUSE;
        return sqlite3_backup_step(m_backup, pages);
    }

    /**
     * @brief Returns the number of pages still to be backed up.
     */
    inline int remaining() const {
        if (!m_backup) return 0;
        return sqlite3_backup_remaining(m_backup);
    }

    /**
     * @brief Returns the total number of pages in the source database.
     */
    inline int pagecount() const {
        if (!m_backup) return 0;
        return sqlite3_backup_pagecount(m_backup);
    }

    /**
     * @brief Manually finish the backup process and release resources early.
     * 
     * This is automatically called by the destructor, but can be called manually
     * to obtain the final result code (e.g., SQLITE_OK or an error code).
     * 
     * @return The final SQLite error code of the backup operation.
     */
    inline int finish() {
        int rc = SQLITE_OK;
        if (m_backup) {
            rc = sqlite3_backup_finish(m_backup);
            m_backup = nullptr;
        }
        return rc;
    }
    
    /**
     * @brief Retrieve the underlying sqlite3_backup pointer for advanced C-API interop.
     */
    inline sqlite3_backup* get() const {
        return m_backup;
    }
};

#endif // SQLITE3_BACKUP_HPP
