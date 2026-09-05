#ifndef SQLITE3_TVF_HPP
#define SQLITE3_TVF_HPP

#include "sqlite3.h"
#include "sqlite3_db.hpp"
#include "sqlite3_aggregate.hpp" // Provides SqliteUdfArgs
#include "sqlite3_allocator.hpp"
#include "sqlite3_ext_state.hpp"

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
public:
    /**
     * @brief Virtual destructor to ensure proper cleanup of derived TVF iterators
     * when destroyed polymorphically via base pointer in sqlite_delete().
     */
    virtual ~SqliteTvfIterator() = default;
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
     * @brief Output the data for a specific column in the current row using SqliteContext.
     * @param ctx The SQLite context to write the result to.
     * @param col_idx The 0-based index of the column being requested.
     */
    virtual void column(SqliteContext ctx, int col_idx) = 0;

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
        
        /**
         * @brief Holds the shared SqliteExtState Entry pointer passed as pClientData
         * during sqlite3_create_module_v2.
         * 
         * STATE INJECTION ARCHITECTURE:
         * Unlike Scalar UDFs and Aggregates where SQLite automatically passes `pApp`
         * to `sqlite3_user_data(ctx)`, SQLite Virtual Table `xColumn` callbacks receive
         * an ephemeral context where `sqlite3_user_data(ctx)` is NULL.
         * By preserving `raw_state` here on the VTab, `xColumn` can inject it directly
         * into `SqliteContext`, enabling O(1) single-instruction `ctx.state<T>()` retrieval.
         */
        void* raw_state;
    };

    // The SQLite cursor instance
    struct Cursor {
        sqlite3_vtab_cursor base;
        SqliteTvfIterator* iter;
    };

    // 1. xConnect / xCreate
    // TVFs are ephemeral (not backed by disk), so Connect and Create do the same thing.
    static int xConnect(sqlite3* db, void* pAux, int /*argc*/, const char* const* /*argv*/, sqlite3_vtab** ppVtab, char** /*pzErr*/) {
        int rc = sqlite3_declare_vtab(db, T::schema());
        if (rc == SQLITE_OK) {
            // Allocate the VTab using our zero-dependency memory allocator
            VTab* pTab = sqlite_new<VTab>();
            if (!pTab) return SQLITE_NOMEM;
            // Capture the shared state pointer (pAux) on the VTab instance for xColumn injection
            pTab->raw_state = pAux;
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
    static int xBestIndex(sqlite3_vtab* /*pVTab*/, sqlite3_index_info* pIdxInfo) {
        int usable_constraints = 0;
        
        for (int i = 0; i < pIdxInfo->nConstraint; i++) {
            // Check if this is an equality constraint (=)
            if (pIdxInfo->aConstraint[i].op == SQLITE_INDEX_CONSTRAINT_EQ) {
                int col = pIdxInfo->aConstraint[i].iColumn;
                // Column 0 is the output value. 
                // Columns 1..N are the hidden argument parameters!
                if (col > 0) {
                    if (!pIdxInfo->aConstraint[i].usable) {
                        return SQLITE_CONSTRAINT;
                    }

                    // Map the constraint to the filter's argv array using contiguous 1-based indexing.
                    usable_constraints++;
                    pIdxInfo->aConstraintUsage[i].argvIndex = usable_constraints;
                    pIdxInfo->aConstraintUsage[i].omit = 1; // Tell SQLite the TVF handled this constraint internally
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
        if (!pCur) return SQLITE_NOMEM;
        // Instantiate the user's iterator
        pCur->iter = sqlite_new<T>();
        if (!pCur->iter) {
            sqlite_delete(pCur);
            return SQLITE_NOMEM;
        }
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
    // Dispatches column output to the C++ iterator, injecting the VTab's raw_state pointer
    // directly into SqliteContext so ctx.state<T>() executes in O(1) single CPU instruction.
    static int xColumn(sqlite3_vtab_cursor* cur, sqlite3_context* ctx, int i) {
        Cursor* pCur = reinterpret_cast<Cursor*>(cur);
        VTab* pTab = reinterpret_cast<VTab*>(cur->pVtab);
        SqliteContext sqlite_ctx(ctx, pTab ? pTab->raw_state : nullptr);
        pCur->iter->column(sqlite_ctx, i);
        return SQLITE_OK;
    }

    // 10. xRowid
    static int xRowid(sqlite3_vtab_cursor* cur, sqlite3_int64* pRowid) {
        Cursor* pCur = reinterpret_cast<Cursor*>(cur);
        *pRowid = pCur->iter->rowid();
        return SQLITE_OK;
    }

    // Statically constructed module table
    static constexpr sqlite3_module module_def = {
        0,              // iVersion
        xConnect,       // xCreate (same as xConnect for ephemeral TVFs)
        xConnect,       // xConnect
        xBestIndex,     // xBestIndex
        xDisconnect,    // xDisconnect
        xDisconnect,    // xDestroy (same as xDisconnect)
        xOpen,          // xOpen
        xClose,         // xClose
        xFilter,        // xFilter
        xNext,          // xNext
        xEof,           // xEof
        xColumn,        // xColumn
        xRowid,         // xRowid
        nullptr,        // xUpdate
        nullptr,        // xBegin
        nullptr,        // xSync
        nullptr,        // xCommit
        nullptr,        // xRollback
        nullptr,        // xFindFunction
        nullptr,        // xRename
        nullptr,        // xSavepoint
        nullptr,        // xRelease
        nullptr,        // xRollbackTo
        nullptr,        // xShadowName
        nullptr         // xIntegrity
    };

    /**
     * @brief Register the C++ TVF iterator as a SQLite Table-Valued Function.
     * 
     * @param db The SQLite database connection (SqliteDatabaseView, SqliteDatabaseOwned, or sqlite3*).
     * @param name The SQL name of the function (e.g., generate_series).
     * @return SQLITE_OK on success, or an error code.
     */
    static int define(SqliteDatabaseView db, const char* name) {
        // Enforce that the user provided a static schema() function
        static_assert(sizeof(T::schema()) > 0, 
            "TVF iterator struct must define a 'static constexpr const char* schema()' method!");

        return sqlite3_create_module(
            db.get(), 
            name, 
            &module_def, 
            nullptr
        );
    }

    /**
     * @brief Register the C++ TVF iterator as a Stateful Table-Valued Function bound to shared state.
     * 
     * Passes raw_state as pClientData into sqlite3_create_module_v2 so that xConnect can capture
     * it on the VTab and inject it into SqliteContext during xColumn evaluation.
     * Automatically registers SqliteExtState<State>::destructor as xDestroy for memory safety.
     * 
     * @tparam State The state struct type managed by SqliteExtState<State>.
     * @param db The SQLite database connection (SqliteDatabaseView, SqliteDatabaseOwned, or sqlite3*).
     * @param name The SQL name of the function.
     * @return SQLITE_OK on success, or an error code.
     */
    template <typename State>
    static int define_with_state(SqliteDatabaseView db, const char* name) {
        static_assert(sizeof(T::schema()) > 0, 
            "TVF iterator struct must define a 'static constexpr const char* schema()' method!");

        // Initialize / retain refcount for this connection's state Entry
        void* raw_state = SqliteExtState<State>::init(db.get());

        // Bind raw_state as pClientData and SqliteExtState destructor as xDestroy
        return sqlite3_create_module_v2(
            db.get(), 
            name, 
            &module_def, 
            raw_state,
            SqliteExtState<State>::destructor
        );
    }
};

// Out-of-line definition for static constexpr member
template <typename T>
constexpr sqlite3_module SqliteTvfModule<T>::module_def;

/**
 * @brief High-level helper class for Table-Valued Function (TVF) registration.
 */
class SqliteTvf {
public:
    /**
     * @brief Register an Object-Oriented C++ Table-Valued Function (TVF) (Stateless).
     * @tparam T The iterator struct/class inheriting from SqliteTvfIterator.
     * @param db The SQLite database connection (SqliteDatabaseView, SqliteDatabaseOwned, or sqlite3*).
     * @param name The SQL name of the TVF.
     * @return SQLITE_OK on success, or an error code.
     */
    template <typename T>
    static inline int define(SqliteDatabaseView db, const char* name) {
        return SqliteTvfModule<T>::define(db.get(), name);
    }

    /**
     * @brief Register an Object-Oriented C++ Table-Valued Function (TVF) bound to shared connection state.
     * @tparam State The state struct type managed by SqliteExtState<State>.
     * @tparam T The iterator struct/class inheriting from SqliteTvfIterator.
     * @param db The SQLite database connection (SqliteDatabaseView, SqliteDatabaseOwned, or sqlite3*).
     * @param name The SQL name of the TVF.
     * @return SQLITE_OK on success, or an error code.
     */
    template <typename State, typename T>
    static inline int define_with_state(SqliteDatabaseView db, const char* name) {
        return SqliteTvfModule<T>::template define_with_state<State>(db, name);
    }
};

#endif // SQLITE3_TVF_HPP
