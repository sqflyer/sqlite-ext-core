#ifndef SQLITE3_VTAB_HPP
#define SQLITE3_VTAB_HPP

#include <sqlite3.h>
#include "sqlite3_value.hpp"
#include "sqlite3_db.hpp"
#include "sqlite3_allocator.hpp"
#include "sqlite3_ext_state.hpp"
#include "sqlite3_vtab_arg.hpp"

/**
 * @brief Bitmask options for Virtual Table capabilities.
 */
enum VTabOptions : unsigned int {
    ReadOnly   = 0,
    Writable   = 1 << 0, // Enables INSERT, UPDATE, DELETE
    Findable   = 1 << 1, // Enables xFindFunction (function overloading)
    HasShadow  = 1 << 2, // Enables xShadowName (protects shadow tables)
    Renameable = 1 << 3, // Enables xRename (ALTER TABLE RENAME)
    Savepoint  = 1 << 4, // Enables xSavepoint, xRelease, xRollbackTo
    Eponymous  = 1 << 5  // Enables Eponymous-only table (xCreate and xDestroy are nullptr)
};

inline constexpr VTabOptions operator|(VTabOptions a, VTabOptions b) {
    return static_cast<VTabOptions>(static_cast<unsigned int>(a) | static_cast<unsigned int>(b));
}

inline constexpr VTabOptions operator&(VTabOptions a, VTabOptions b) {
    return static_cast<VTabOptions>(static_cast<unsigned int>(a) & static_cast<unsigned int>(b));
}

inline constexpr VTabOptions operator^(VTabOptions a, VTabOptions b) {
    return static_cast<VTabOptions>(static_cast<unsigned int>(a) ^ static_cast<unsigned int>(b));
}

inline constexpr VTabOptions operator~(VTabOptions a) {
    return static_cast<VTabOptions>(~static_cast<unsigned int>(a));
}

inline VTabOptions& operator|=(VTabOptions& a, VTabOptions b) {
    a = a | b;
    return a;
}

inline VTabOptions& operator&=(VTabOptions& a, VTabOptions b) {
    a = a & b;
    return a;
}

inline VTabOptions& operator^=(VTabOptions& a, VTabOptions b) {
    a = a ^ b;
    return a;
}

/**
 * @brief Alias for an SQLite scalar function pointer.
 */
using SqliteFuncPtr = void (*)(sqlite3_context*, int, sqlite3_value**);

/**
 * @brief Compile-time trampoline to wrap a C++ UDF into an SQLite C UDF.
 * 
 * Usage in findFunction:
 *   pxFunc = SqliteUdfWrapper<my_cxx_function>;
 *   ppArg = my_custom_state;
 */
template <void (*CxxFunc)(SqliteContext&, SqliteUdfArgs)>
inline void SqliteUdfWrapper(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    SqliteContext cxx_ctx(ctx);
    SqliteUdfArgs cxx_args(argc, argv);
    CxxFunc(cxx_ctx, cxx_args);
}

/**
 * @brief Represents a returned UDF from findFunction.
 */
struct SqliteFunctionDef {
    SqliteFuncPtr func;
    void* arg;

    inline SqliteFunctionDef() : func(nullptr), arg(nullptr) {}
    inline SqliteFunctionDef(SqliteFuncPtr f, void* a) : func(f), arg(a) {}

    template<void (*CxxFunc)(SqliteContext&, SqliteUdfArgs)>
    static SqliteFunctionDef wrap(void* arg = nullptr) {
        return SqliteFunctionDef(SqliteUdfWrapper<CxxFunc>, arg);
    }

    inline bool is_valid() const { return func != nullptr; }
};

class SqliteVTable;

/**
 * @brief Encapsulates arguments and out-parameters for VTable connection.
 */
class SqliteConnectArgs {
private:
    sqlite3* m_db;
    void* m_pAux;
    int m_argc;
    const char* const* m_argv;
    SqliteVTable* m_instance;
    const char* m_err;

public:
    inline SqliteConnectArgs(sqlite3* db, void* pAux, int argc, const char* const* argv)
        : m_db(db), m_pAux(pAux), m_argc(argc), m_argv(argv), m_instance(nullptr), m_err(nullptr) {}

    inline sqlite3* db() const { return m_db; }
    inline void* aux_data() const { return m_pAux; }

    inline int size() const { return m_argc; }
    inline const char* operator[](int i) const { return m_argv[i]; }
    inline const char* const* argv() const { return m_argv; }

    /**
     * @brief Retrieve extension shared state directly from connect args.
     * @tparam State The user-defined state struct.
     * @return Strongly-typed State* pointer, or nullptr.
     */
    template <typename State>
    inline State* state() const {
        return SqliteExtState<State>::from_ptr(m_pAux);
    }

    template <typename T>
    inline void set_instance(T* instance) { m_instance = static_cast<SqliteVTable*>(instance); }
    inline SqliteVTable* get_instance() const { return m_instance; }

    inline void set_error(const char* err) { m_err = err; }
    inline const char* get_error() const { return m_err; }
};

/**
 * @brief Helper for parsing xBestIndex constraints.
 */
class SqliteIndexInfo {
private:
    sqlite3_index_info* m_info;

public:
    inline SqliteIndexInfo(sqlite3_index_info* info) : m_info(info) {}

    // Number of constraints
    inline int num_constraints() const { return m_info->nConstraint; }

    // Number of order by clauses
    inline int num_order_by() const { return m_info->nOrderBy; }

    // Access raw constraints
    inline const sqlite3_index_info::sqlite3_index_constraint& constraint(int i) const { return m_info->aConstraint[i]; }
    inline const sqlite3_index_info::sqlite3_index_orderby& order_by(int i) const { return m_info->aOrderBy[i]; }
    
    // Modify constraint usage
    inline sqlite3_index_info::sqlite3_index_constraint_usage& usage(int i) { return m_info->aConstraintUsage[i]; }

    // Set estimated cost
    inline void set_estimated_cost(double cost) { m_info->estimatedCost = cost; }
    inline void set_estimated_rows(sqlite3_int64 rows) { m_info->estimatedRows = rows; }

    // Set index info to pass to xFilter
    inline void set_idx_num(int idxNum) { m_info->idxNum = idxNum; }
    inline void set_idx_str(char* idxStr, int needToFree = 0) { 
        m_info->idxStr = idxStr; 
        m_info->needToFreeIdxStr = needToFree;
    }
};

/**
 * @brief Base class for C++ Virtual Table Cursors.
 */
class SqliteVTabCursor {
public:
    virtual ~SqliteVTabCursor() = default;

    // Provide class-specific delete to prevent linker errors with -nostdlib++
    void operator delete(void* p) noexcept { sqlite3_free(p); }
    void operator delete(void* p, size_t) noexcept { sqlite3_free(p); }

    /**
     * @brief Filter the rows based on the index chosen by xBestIndex.
     */
    virtual int filter(int idxNum, const char* idxStr, SqliteUdfArgs args) = 0;

    /**
     * @brief Advance the cursor to the next row.
     */
    virtual int next() = 0;

    /**
     * @brief Check if the cursor has reached the end of the data.
     */
    virtual bool eof() = 0;

    /**
     * @brief Extract data for the specified column (N) into the context.
     */
    virtual int column(SqliteContext& ctx, int N) = 0;

    /**
     * @brief Return the rowid of the current row.
     */
    virtual int rowid(sqlite3_int64& pRowid) = 0;
};

/**
 * @brief Base class for C++ Virtual Tables.
 */
class SqliteVTable {
protected:
    sqlite3* m_db;
    
public:
    inline explicit SqliteVTable(sqlite3* db) : m_db(db) {}
    virtual ~SqliteVTable() = default;

    // Provide class-specific delete to prevent linker errors with -nostdlib++
    void operator delete(void* p) noexcept { sqlite3_free(p); }
    void operator delete(void* p, size_t) noexcept { sqlite3_free(p); }

    /**
     * @brief Set the error message for the virtual table. Memory is managed by SQLite.
     * The wrapper will automatically assign it to the sqlite3_vtab base struct.
     */
    virtual const char* get_error_message() const { return nullptr; }

    /**
     * @brief Query planner optimization logic.
     */
    virtual int bestIndex(SqliteIndexInfo& info) = 0;

    /**
     * @brief Allocate a new cursor for this table.
     */
    virtual SqliteVTabCursor* open() = 0;

    /**
     * @brief (Writeable Only) Modifies data in the virtual table (INSERT, UPDATE, DELETE).
     */
    virtual int update(SqliteUdfArgs args, sqlite3_int64* pRowid) {
        (void)args; (void)pRowid;
        return SQLITE_READONLY;
    }

    /** @brief (Writeable Only) Starts a transaction on the virtual table. */
    virtual int begin() { return SQLITE_OK; }

    /** @brief (Writeable Only) Syncs data before a commit. */
    virtual int sync() { return SQLITE_OK; }

    /** @brief (Writeable Only) Commits a transaction. */
    virtual int commit() { return SQLITE_OK; }

    /** @brief (Writeable Only) Rolls back a transaction. */
    virtual int rollback() { return SQLITE_OK; }

    /** 
     * @brief (Findable Only) Intercepts function calls for this virtual table.
     * @return A SqliteFunctionDef. Return {} to not intercept.
     */
    virtual SqliteFunctionDef findFunction(int nArg, const char* zName) {
        (void)nArg; (void)zName;
        return {};
    }

    /** 
     * @brief (Renameable Only) Called when the virtual table is renamed via ALTER TABLE.
     */
    virtual int rename(const char* zNewName) {
        (void)zNewName;
        return SQLITE_OK;
    }

    /** 
     * @brief (HasShadow Only) Called to check if a table is a shadow table for this virtual table.
     * Must be implemented as a static method hiding this base implementation.
     */
    static int shadowName(const char* zName) {
        (void)zName;
        return 0;
    }

    /** @brief (Savepoint Only) Initiates a savepoint. */
    virtual int savepoint(int iSavepoint) {
        (void)iSavepoint;
        return SQLITE_OK;
    }

    /** @brief (Savepoint Only) Releases a savepoint. */
    virtual int release(int iSavepoint) {
        (void)iSavepoint;
        return SQLITE_OK;
    }

    /** @brief (Savepoint Only) Rolls back to a savepoint. */
    virtual int rollbackTo(int iSavepoint) {
        (void)iSavepoint;
        return SQLITE_OK;
    }
};

/**
 * @brief Registers a C++ Virtual Table with the SQLite connection.
 * 
 * VTableType MUST inherit from SqliteVTable and provide a static method:
 *   static int connect(SqliteConnectArgs& args);
 * Which creates the SQL schema for the table and sets the instance in args.
 * 
 * @tparam VTableType The C++ class implementing the virtual table.
 * @tparam Options Bitmask of VTabOptions representing the capabilities of the table.
 */
template<typename VTableType, VTabOptions Options = VTabOptions::ReadOnly>
class SqliteVTabModule {
private:
    struct TableWrapper {
        sqlite3_vtab base;
        VTableType* instance;
        void* raw_state;
    };

    struct CursorWrapper {
        sqlite3_vtab_cursor base;
        SqliteVTabCursor* instance;
    };

    static inline void set_error_message(sqlite3_vtab* pVTab, VTableType* instance) {
        if (pVTab && instance) {
            const char* err = instance->get_error_message();
            if (err && !pVTab->zErrMsg) {
                pVTab->zErrMsg = sqlite3_mprintf("%s", err);
            }
        }
    }

    static int xCreate(sqlite3* db, void* pAux, int argc, const char* const* argv,
                       sqlite3_vtab** ppVTab, char** pzErr) {
        SqliteConnectArgs args(db, pAux, argc, argv);
        int rc = VTableType::connect(args);
        
        SqliteVTable* instance = args.get_instance();
        const char* err = args.get_error();
        if (err && pzErr && !*pzErr) {
            *pzErr = sqlite3_mprintf("%s", err);
        }
        if (rc == SQLITE_OK && instance != nullptr) {
            TableWrapper* wrapper = sqlite_new<TableWrapper>();
            if (!wrapper) {
                sqlite_delete(instance);
                return SQLITE_NOMEM;
            }
            wrapper->base.pModule = nullptr;
            wrapper->base.nRef = 0;
            wrapper->base.zErrMsg = nullptr;
            wrapper->instance = static_cast<VTableType*>(instance);
            wrapper->raw_state = pAux;
            *ppVTab = &wrapper->base;
        }
        return rc;
    }

    static int xConnect(sqlite3* db, void* pAux, int argc, const char* const* argv,
                        sqlite3_vtab** ppVTab, char** pzErr) {
        return xCreate(db, pAux, argc, argv, ppVTab, pzErr);
    }

    static int xBestIndex(sqlite3_vtab* pVTab, sqlite3_index_info* pInfo) {
        TableWrapper* wrapper = reinterpret_cast<TableWrapper*>(pVTab);
        SqliteIndexInfo info(pInfo);
        int rc = wrapper->instance->bestIndex(info);
        set_error_message(pVTab, wrapper->instance);
        return rc;
    }

    static int xDisconnect(sqlite3_vtab* pVTab) {
        TableWrapper* wrapper = reinterpret_cast<TableWrapper*>(pVTab);
        if (wrapper->base.zErrMsg) {
            sqlite3_free(wrapper->base.zErrMsg);
        }
        sqlite_delete(wrapper->instance);
        sqlite_delete(wrapper);
        return SQLITE_OK;
    }

    static int xDestroy(sqlite3_vtab* pVTab) {
        return xDisconnect(pVTab);
    }

    static int xOpen(sqlite3_vtab* pVTab, sqlite3_vtab_cursor** ppCursor) {
        TableWrapper* wrapper = reinterpret_cast<TableWrapper*>(pVTab);
        SqliteVTabCursor* instance = wrapper->instance->open();
        if (!instance) {
            return SQLITE_NOMEM;
        }
        
        CursorWrapper* cursor = sqlite_new<CursorWrapper>();
        if (!cursor) {
            sqlite_delete(instance);
            return SQLITE_NOMEM;
        }
        
        cursor->base.pVtab = pVTab;
        cursor->instance = instance;
        *ppCursor = &cursor->base;
        return SQLITE_OK;
    }

    static int xClose(sqlite3_vtab_cursor* pCursor) {
        CursorWrapper* wrapper = reinterpret_cast<CursorWrapper*>(pCursor);
        sqlite_delete(wrapper->instance);
        sqlite_delete(wrapper);
        return SQLITE_OK;
    }

    static int xFilter(sqlite3_vtab_cursor* pCursor, int idxNum, const char* idxStr,
                       int argc, sqlite3_value** argv) {
        CursorWrapper* wrapper = reinterpret_cast<CursorWrapper*>(pCursor);
        int rc = wrapper->instance->filter(idxNum, idxStr, SqliteUdfArgs(argc, argv));
        TableWrapper* tab = reinterpret_cast<TableWrapper*>(pCursor->pVtab);
        if (tab) set_error_message(pCursor->pVtab, tab->instance);
        return rc;
    }

    static int xNext(sqlite3_vtab_cursor* pCursor) {
        CursorWrapper* wrapper = reinterpret_cast<CursorWrapper*>(pCursor);
        int rc = wrapper->instance->next();
        TableWrapper* tab = reinterpret_cast<TableWrapper*>(pCursor->pVtab);
        if (tab) set_error_message(pCursor->pVtab, tab->instance);
        return rc;
    }

    static int xEof(sqlite3_vtab_cursor* pCursor) {
        CursorWrapper* wrapper = reinterpret_cast<CursorWrapper*>(pCursor);
        return wrapper->instance->eof() ? 1 : 0;
    }

    static int xColumn(sqlite3_vtab_cursor* pCursor, sqlite3_context* ctx, int N) {
        CursorWrapper* wrapper = reinterpret_cast<CursorWrapper*>(pCursor);
        TableWrapper* tab = reinterpret_cast<TableWrapper*>(pCursor->pVtab);
        SqliteContext sqlite_ctx(ctx, tab ? tab->raw_state : nullptr);
        return wrapper->instance->column(sqlite_ctx, N);
    }

    static int xRowid(sqlite3_vtab_cursor* pCursor, sqlite3_int64* pRowid) {
        CursorWrapper* wrapper = reinterpret_cast<CursorWrapper*>(pCursor);
        int rc = wrapper->instance->rowid(*pRowid);
        TableWrapper* tab = reinterpret_cast<TableWrapper*>(pCursor->pVtab);
        if (tab) set_error_message(pCursor->pVtab, tab->instance);
        return rc;
    }

    static int xUpdate(sqlite3_vtab* pVTab, int argc, sqlite3_value** argv, sqlite3_int64* pRowid) {
        TableWrapper* wrapper = reinterpret_cast<TableWrapper*>(pVTab);
        int rc = wrapper->instance->update(SqliteUdfArgs(argc, argv), pRowid);
        set_error_message(pVTab, wrapper->instance);
        return rc;
    }

    static int xBegin(sqlite3_vtab* pVTab) {
        TableWrapper* wrapper = reinterpret_cast<TableWrapper*>(pVTab);
        int rc = wrapper->instance->begin();
        set_error_message(pVTab, wrapper->instance);
        return rc;
    }

    static int xSync(sqlite3_vtab* pVTab) {
        TableWrapper* wrapper = reinterpret_cast<TableWrapper*>(pVTab);
        int rc = wrapper->instance->sync();
        set_error_message(pVTab, wrapper->instance);
        return rc;
    }

    static int xCommit(sqlite3_vtab* pVTab) {
        TableWrapper* wrapper = reinterpret_cast<TableWrapper*>(pVTab);
        int rc = wrapper->instance->commit();
        set_error_message(pVTab, wrapper->instance);
        return rc;
    }

    static int xRollback(sqlite3_vtab* pVTab) {
        TableWrapper* wrapper = reinterpret_cast<TableWrapper*>(pVTab);
        int rc = wrapper->instance->rollback();
        set_error_message(pVTab, wrapper->instance);
        return rc;
    }

    static int xFindFunction(sqlite3_vtab* pVTab, int nArg, const char* zName,
                             void (**pxFunc)(sqlite3_context*, int, sqlite3_value**),
                             void** ppArg) {
        SqliteFunctionDef def = reinterpret_cast<TableWrapper*>(pVTab)->instance->findFunction(nArg, zName);
        if (def.is_valid()) {
            *pxFunc = reinterpret_cast<void (*)(sqlite3_context*, int, sqlite3_value**)>(def.func);
            *ppArg = def.arg;
            return 1;
        }
        return 0;
    }

    static int xRename(sqlite3_vtab* pVTab, const char* zNew) {
        TableWrapper* wrapper = reinterpret_cast<TableWrapper*>(pVTab);
        int rc = wrapper->instance->rename(zNew);
        set_error_message(pVTab, wrapper->instance);
        return rc;
    }

    static int xShadowName(const char* zName) {
        return VTableType::shadowName(zName);
    }

    static int xSavepoint(sqlite3_vtab* pVTab, int iSavepoint) {
        TableWrapper* wrapper = reinterpret_cast<TableWrapper*>(pVTab);
        int rc = wrapper->instance->savepoint(iSavepoint);
        set_error_message(pVTab, wrapper->instance);
        return rc;
    }

    static int xRelease(sqlite3_vtab* pVTab, int iSavepoint) {
        TableWrapper* wrapper = reinterpret_cast<TableWrapper*>(pVTab);
        int rc = wrapper->instance->release(iSavepoint);
        set_error_message(pVTab, wrapper->instance);
        return rc;
    }

    static int xRollbackTo(sqlite3_vtab* pVTab, int iSavepoint) {
        TableWrapper* wrapper = reinterpret_cast<TableWrapper*>(pVTab);
        int rc = wrapper->instance->rollbackTo(iSavepoint);
        set_error_message(pVTab, wrapper->instance);
        return rc;
    }

public:
    /**
     * @brief Registers the virtual table module.
     * @param db The SQLite database connection.
     * @param module_name The name of the module (e.g., used in CREATE VIRTUAL TABLE x USING module_name)
     * @param pAux Optional auxiliary data passed to xCreate/xConnect
     * @param xDestroyAux Optional destructor for pAux
     * @param is_eponymous If true, creates an eponymous-only virtual table.
     */
    static constexpr bool is_writable = ((Options & VTabOptions::Writable) != VTabOptions::ReadOnly) || ((Options & VTabOptions::Savepoint) != VTabOptions::ReadOnly);
    static constexpr bool is_eponymous = (Options & VTabOptions::Eponymous) != VTabOptions::ReadOnly;
    static constexpr int IVER = ((Options & VTabOptions::HasShadow) != VTabOptions::ReadOnly) ? 3 : (((Options & VTabOptions::Savepoint) != VTabOptions::ReadOnly) ? 2 : 1);

    static constexpr sqlite3_module module_def = {
        IVER, // iVersion
        is_eponymous ? nullptr : xCreate,
        xConnect,
        xBestIndex,
        xDisconnect,
        is_eponymous ? nullptr : xDestroy,
        xOpen,
        xClose,
        xFilter,
        xNext,
        xEof,
        xColumn,
        xRowid,
        is_writable ? xUpdate : nullptr,
        is_writable ? xBegin : nullptr,
        is_writable ? xSync : nullptr,
        is_writable ? xCommit : nullptr,
        is_writable ? xRollback : nullptr,
        ((Options & VTabOptions::Findable) != VTabOptions::ReadOnly) ? xFindFunction : nullptr,
        ((Options & VTabOptions::Renameable) != VTabOptions::ReadOnly) ? xRename : nullptr,
        ((Options & VTabOptions::Savepoint) != VTabOptions::ReadOnly) ? xSavepoint : nullptr,
        ((Options & VTabOptions::Savepoint) != VTabOptions::ReadOnly) ? xRelease : nullptr,
        ((Options & VTabOptions::Savepoint) != VTabOptions::ReadOnly) ? xRollbackTo : nullptr,
        ((Options & VTabOptions::HasShadow) != VTabOptions::ReadOnly) ? xShadowName : nullptr,
        nullptr  // xIntegrity
    };

    /**
     * @brief Registers the virtual table module.
     * @param db The SQLite database connection (SqliteDatabaseView, SqliteDatabaseOwned, or sqlite3*).
     * @param module_name The name of the module (e.g., used in CREATE VIRTUAL TABLE x USING module_name).
     * @param pAux Optional auxiliary data passed to xCreate/xConnect.
     * @param xDestroyAux Optional destructor for pAux.
     * @return SQLITE_OK on success, or an error code.
     */
    static int register_module(SqliteDatabaseView db, const char* module_name, void* pAux = nullptr, void(*xDestroyAux)(void*) = nullptr) {
        if (xDestroyAux) {
            return sqlite3_create_module_v2(db.get(), module_name, &module_def, pAux, xDestroyAux);
        } else {
            return sqlite3_create_module(db.get(), module_name, &module_def, pAux);
        }
    }

    /**
     * @brief Registers the C++ Virtual Table module with SQLite bound to shared state.
     * 
     * @tparam State The state struct type managed by SqliteExtState<State>.
     * @param db The SQLite database connection (SqliteDatabaseView, SqliteDatabaseOwned, or sqlite3*).
     * @param module_name The SQL name of the virtual table module.
     * @return SQLITE_OK on success, or an error code.
     */
    template <typename State>
    static int register_module_with_state(SqliteDatabaseView db, const char* module_name) {
        void* raw_state = SqliteExtState<State>::init(db.get());
        return register_module(db.get(), module_name, raw_state, SqliteExtState<State>::destructor);
    }
};

// Out-of-line definition for static constexpr member in C++11
template <typename VTableType, VTabOptions Options>
constexpr sqlite3_module SqliteVTabModule<VTableType, Options>::module_def;

/**
 * @brief High-level helper class for Virtual Table registration.
 */
class SqliteVTab {
public:
    /**
     * @brief Register a C++ Virtual Table module with SQLite (Stateless).
     * @tparam VTableType The C++ class implementing the virtual table (inheriting from SqliteVTable).
     * @tparam Options Bitmask of VTabOptions (e.g. VTabOptions::Writable, VTabOptions::Eponymous).
     * @param db The SQLite database connection (SqliteDatabaseView, SqliteDatabaseOwned, or sqlite3*).
     * @param module_name The SQL virtual table module name.
     * @return SQLITE_OK on success, or an SQLite error code.
     */
    template <typename VTableType, VTabOptions Options = VTabOptions::ReadOnly>
    static inline int define(SqliteDatabaseView db, const char* module_name) {
        return SqliteVTabModule<VTableType, Options>::register_module(db, module_name);
    }

    /**
     * @brief Register a C++ Virtual Table module with SQLite bound to shared connection state.
     * @tparam State The user-defined state struct type.
     * @tparam VTableType The C++ class implementing the virtual table (inheriting from SqliteVTable).
     * @tparam Options Bitmask of VTabOptions (e.g. VTabOptions::Writable, VTabOptions::Eponymous).
     * @param db The SQLite database connection (SqliteDatabaseView, SqliteDatabaseOwned, or sqlite3*).
     * @param module_name The SQL virtual table module name.
     * @return SQLITE_OK on success, or an SQLite error code.
     */
    template <typename State, typename VTableType, VTabOptions Options = VTabOptions::ReadOnly>
    static inline int define_with_state(SqliteDatabaseView db, const char* module_name) {
        return SqliteVTabModule<VTableType, Options>::template register_module_with_state<State>(db, module_name);
    }
};

#endif // SQLITE3_VTAB_HPP
