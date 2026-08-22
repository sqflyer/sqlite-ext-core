#ifndef SQLITE3_TVF_HPP
#define SQLITE3_TVF_HPP

#include "sqlite3.h"
#include "sqlite3_aggregate.hpp" // Provides SqliteUdfArgs
#include "sqlite3_allocator.hpp"

// ============================================================================
// TVF (TABLE-VALUED FUNCTION) FRAMEWORK
// ============================================================================

/**
 * @brief Base class for Table-Valued Function (TVF) Iterators.
 * 
 * To create a TVF, inherit from this class and implement the pure virtual methods.
 * You must also provide a `static constexpr const char* schema()` method in your derived class.
 */
class SqliteTvfIterator {
protected:
    // Protected non-virtual destructor prevents deletion via base pointer,
    // avoiding the need for a global operator delete while silencing -Wnon-virtual-dtor
    ~SqliteTvfIterator() = default;

public:
    /**
     * @brief Computes the estimated cost of the TVF given the number of bound arguments.
     * 
     * By default, this returns a cost inversely proportional to the number of bound arguments,
     * forcing SQLite to prioritize query plans that provide the most arguments.
     * 
     * To provide a custom cost, simply define a `static double estimated_cost(int)` 
     * method in your derived class. C++ name hiding will automatically route to it!
     */
    static double estimated_cost(int usable_args) {
        return 100000.0 / (usable_args + 1);
    }

    /**
     * @brief Called when the TVF is executed to initialize the iterator.
     * @param args The arguments passed to the TVF (derived from hidden columns).
     */
    virtual void init(SqliteUdfArgs args) = 0;

    /**
     * @brief Called to advance the iterator to the next row.
     */
    virtual void next() = 0;

    /**
     * @brief Check if the iterator has finished producing rows.
     * @return true if there are no more rows, false otherwise.
     */
    virtual bool eof() const = 0;

    /**
     * @brief Output the data for a specific column in the current row.
     * @param ctx The SQLite context to write the result to.
     * @param col_idx The 0-based index of the column being requested.
     */
    virtual void column(sqlite3_context* ctx, int col_idx) = 0;

    /**
     * @brief Provide a unique row ID for the current row.
     * @return The 64-bit integer row ID.
     */
    virtual sqlite3_int64 rowid() const = 0;
};

// ============================================================================
// INTERNAL MODULE SCAFFOLDING
// ============================================================================

/**
 * @brief Internal framework class that bridges the SQLite C Virtual Table API
 * with the C++ SqliteTvfIterator interface.
 * 
 * @tparam T The user-defined iterator class that inherits from SqliteTvfIterator.
 */
template <typename T>
struct SqliteTvfModule {
    
    // The SQLite virtual table instance
    struct VTab {
        sqlite3_vtab base;
    };

    // The SQLite cursor instance
    struct Cursor {
        sqlite3_vtab_cursor base;
        SqliteTvfIterator* iter;
    };

    // 1. xConnect / xCreate
    // TVFs are ephemeral (not backed by disk), so Connect and Create do the same thing.
    static int xConnect(sqlite3* db, void* /*pAux*/, int /*argc*/, const char* const* /*argv*/, sqlite3_vtab** ppVtab, char** /*pzErr*/) {
        int rc = sqlite3_declare_vtab(db, T::schema());
        if (rc == SQLITE_OK) {
            // Allocate the VTab using our zero-dependency memory allocator
            VTab* pTab = sqlite_new<VTab>();
            *ppVtab = &pTab->base;
        }
        return rc;
    }

    // 2. xDisconnect / xDestroy
    static int xDisconnect(sqlite3_vtab* pVtab) {
        VTab* pTab = reinterpret_cast<VTab*>(pVtab);
        sqlite_delete(pTab);
        return SQLITE_OK;
    }

    // 3. xBestIndex
    // This automagically maps SQL equality constraints into xFilter arguments.
    // If a user queries `my_tvf(10, 20)`, SQLite presents them as equality constraints
    // on the hidden columns in the WHERE clause.
    static int xBestIndex(sqlite3_vtab* /*pVtab*/, sqlite3_index_info* pIdxInfo) {
        int usable_constraints = 0;
        for (int i = 0; i < pIdxInfo->nConstraint; ++i) {
            if (pIdxInfo->aConstraint[i].op == SQLITE_INDEX_CONSTRAINT_EQ) {
                int col = pIdxInfo->aConstraint[i].iColumn;
                if (col > 0) {
                    if (!pIdxInfo->aConstraint[i].usable) {
                        return SQLITE_CONSTRAINT;
                    }

                    pIdxInfo->aConstraintUsage[i].argvIndex = col;
                    pIdxInfo->aConstraintUsage[i].omit = 1; 
                    usable_constraints++;
                }
            }
        }
        
        // Defer to the user's TVF class to calculate the cost.
        // If they didn't define one, it falls back to SqliteTvfIterator::estimated_cost().
        pIdxInfo->estimatedCost = T::estimated_cost(usable_constraints);
        return SQLITE_OK;
    }

    // 4. xOpen
    static int xOpen(sqlite3_vtab* /*pVtab*/, sqlite3_vtab_cursor** ppCursor) {
        Cursor* pCur = sqlite_new<Cursor>();
        // Instantiate the user's iterator
        pCur->iter = sqlite_new<T>();
        *ppCursor = &pCur->base;
        return SQLITE_OK;
    }

    // 5. xClose
    static int xClose(sqlite3_vtab_cursor* cur) {
        Cursor* pCur = reinterpret_cast<Cursor*>(cur);
        // Cast back to the exact type T* so we don't need a virtual destructor!
        sqlite_delete(static_cast<T*>(pCur->iter));
        sqlite_delete(pCur);
        return SQLITE_OK;
    }

    // 6. xFilter
    static int xFilter(sqlite3_vtab_cursor* cur, int /*idxNum*/, const char* /*idxStr*/, int argc, sqlite3_value** argv) {
        Cursor* pCur = reinterpret_cast<Cursor*>(cur);
        // Wrap raw SQLite values into our bounds-safe args object
        SqliteUdfArgs args(argc, argv);
        pCur->iter->init(args);
        return SQLITE_OK;
    }

    // 7. xNext
    static int xNext(sqlite3_vtab_cursor* cur) {
        Cursor* pCur = reinterpret_cast<Cursor*>(cur);
        pCur->iter->next();
        return SQLITE_OK;
    }

    // 8. xEof
    static int xEof(sqlite3_vtab_cursor* cur) {
        Cursor* pCur = reinterpret_cast<Cursor*>(cur);
        return pCur->iter->eof() ? 1 : 0;
    }

    // 9. xColumn
    static int xColumn(sqlite3_vtab_cursor* cur, sqlite3_context* ctx, int i) {
        Cursor* pCur = reinterpret_cast<Cursor*>(cur);
        pCur->iter->column(ctx, i);
        return SQLITE_OK;
    }

    // 10. xRowid
    static int xRowid(sqlite3_vtab_cursor* cur, sqlite_int64* pRowid) {
        Cursor* pCur = reinterpret_cast<Cursor*>(cur);
        *pRowid = pCur->iter->rowid();
        return SQLITE_OK;
    }

    // The static module instance to register with SQLite
    static sqlite3_module module;

    /**
     * @brief Define and register this TVF with SQLite.
     * @param db The SQLite database connection.
     * @param name The name of the table-valued function.
     * @return SQLITE_OK on success, or an error code.
     */
    static inline int define(sqlite3* db, const char* name) {
        // Enforce strict API contract at compile-time (schema must exist)
        static_assert(sizeof(T::schema()) > 0, 
                      "TVF class must define: static constexpr const char* schema()");

        return sqlite3_create_module(db, name, &module, nullptr);
    }
};

// Define the static module instance
template <typename T>
sqlite3_module SqliteTvfModule<T>::module = {
    0,                      // iVersion
    SqliteTvfModule<T>::xConnect,    // xCreate (Virtual Tables normally use Create to make backing tables)
    SqliteTvfModule<T>::xConnect,    // xConnect
    SqliteTvfModule<T>::xBestIndex,  // xBestIndex
    SqliteTvfModule<T>::xDisconnect, // xDisconnect
    SqliteTvfModule<T>::xDisconnect, // xDestroy
    SqliteTvfModule<T>::xOpen,       // xOpen
    SqliteTvfModule<T>::xClose,      // xClose
    SqliteTvfModule<T>::xFilter,     // xFilter
    SqliteTvfModule<T>::xNext,       // xNext
    SqliteTvfModule<T>::xEof,        // xEof
    SqliteTvfModule<T>::xColumn,     // xColumn
    SqliteTvfModule<T>::xRowid,      // xRowid
    nullptr,                // xUpdate
    nullptr,                // xBegin
    nullptr,                // xSync
    nullptr,                // xCommit
    nullptr,                // xRollback
    nullptr,                // xFindFunction
    nullptr,                // xRename
    nullptr,                // xSavepoint
    nullptr,                // xRelease
    nullptr,                // xRollbackTo
    nullptr,                // xShadowName
    nullptr                 // xIntegrity
};

#endif // SQLITE3_TVF_HPP
