#ifndef SQLITE3_TVF_CORO_HPP
#define SQLITE3_TVF_CORO_HPP

/**
 * @file sqlite3_tvf_coro.hpp
 * @brief Zero-boilerplate Coroutine-based Table-Valued Function (TVF) Framework.
 *
 * Provides a declarative, zero-boilerplate C++ framework for authoring SQLite
 * Table-Valued Functions (eponymous virtual tables) using cooperative coroutines.
 * Developers write a single generator function that emits values via `yield(val)`
 * (using stackful `SqliteFiberGenerator<T>`) or `co_yield val` (using stackless
 * `SqliteGenerator<T>`), completely eliminating manual cursor state machines,
 * step counters, and switch-case dispatch tables.
 *
 * Key Architectural Highlights:
 * - 100% Freestanding: Zero standard library dependencies (`-nostdlib++` compliant).
 * - Automatic Column Multiplexing: Natively routes scalar types, `SqliteValueOwned`,
 *   `SqliteValueTuple<N>`, and `SqliteValueVec<N>` to SQLite output contexts.
 * - Profiled Allocation: Coroutine state frames and virtual table cursors are allocated
 *   strictly via `sqlite3_malloc64` and accounted for in `sqlite3_memory_used()`.
 * - State Injection: Supports shared per-connection database state via `SqliteExtState`.
 */

#include "sqlite3.h"
#include "sqlite3_db.hpp"
#include "sqlite3_aggregate.hpp"
#include "sqlite3_allocator.hpp"
#include "sqlite3_row.hpp"
#include "sqlite3_ext_state.hpp"
#include "async/sqlite3_coro.hpp"

// ============================================================================
// COLUMN DISPATCH TRAITS
// ============================================================================

/**
 * @struct SqliteTvfColumnWriter
 * @brief Type traits mapping yielded generator items to SQLite column outputs.
 * 
 * Provides static `write(ctx, val, col_idx)` methods specialized for scalar types,
 * string views, and multi-column row containers.
 *
 * @tparam ValueType The value type yielded by the user's coroutine generator.
 */
template <typename ValueType>
struct SqliteTvfColumnWriter {
    /**
     * @brief Fallback writer converting general values via SqliteValueView.
     * @param ctx The SQLite context receiving the column value.
     * @param val The yielded item.
     * @param col_idx 0-based column index requested by SQLite.
     */
    static inline void write(SqliteContext ctx, const ValueType& val, int col_idx) {
        if (col_idx == 0) {
            SqliteValueView(val).result(ctx.get());
        }
    }
};

/**
 * @brief Specialization of SqliteTvfColumnWriter for 64-bit integer output.
 */
template <>
struct SqliteTvfColumnWriter<sqlite3_int64> {
    /**
     * @brief Writes a 64-bit integer to column 0.
     * @param ctx The SQLite output context.
     * @param val The 64-bit integer value.
     * @param col_idx 0-based column index.
     */
    static inline void write(SqliteContext ctx, const sqlite3_int64& val, int col_idx) {
        if (col_idx == 0) ctx.result_int64(val);
    }
};

/**
 * @brief Specialization of SqliteTvfColumnWriter for 32-bit integer output.
 */
template <>
struct SqliteTvfColumnWriter<int> {
    /**
     * @brief Writes a 32-bit integer to column 0.
     * @param ctx The SQLite output context.
     * @param val The 32-bit integer value.
     * @param col_idx 0-based column index.
     */
    static inline void write(SqliteContext ctx, const int& val, int col_idx) {
        if (col_idx == 0) ctx.result_int(val);
    }
};

/**
 * @brief Specialization of SqliteTvfColumnWriter for 64-bit floating point output.
 */
template <>
struct SqliteTvfColumnWriter<double> {
    /**
     * @brief Writes a 64-bit floating point double to column 0.
     * @param ctx The SQLite output context.
     * @param val The floating point double value.
     * @param col_idx 0-based column index.
     */
    static inline void write(SqliteContext ctx, const double& val, int col_idx) {
        if (col_idx == 0) ctx.result_double(val);
    }
};

/**
 * @brief Specialization of SqliteTvfColumnWriter for string view output.
 */
template <>
struct SqliteTvfColumnWriter<SqliteStringView> {
    /**
     * @brief Writes a UTF-8 string slice to column 0.
     * @param ctx The SQLite output context.
     * @param val The string view reference.
     * @param col_idx 0-based column index.
     */
    static inline void write(SqliteContext ctx, const SqliteStringView& val, int col_idx) {
        if (col_idx == 0) ctx.result_text(val.data(), val.length());
    }
};

/**
 * @brief Specialization of SqliteTvfColumnWriter for owned scalar values.
 */
template <>
struct SqliteTvfColumnWriter<SqliteValueOwned> {
    /**
     * @brief Writes an owned polymorphic value to column 0.
     * @param ctx The SQLite output context.
     * @param val The owned value object.
     * @param col_idx 0-based column index.
     */
    static inline void write(SqliteContext ctx, const SqliteValueOwned& val, int col_idx) {
        if (col_idx == 0) val.result(ctx.get());
    }
};

/**
 * @brief Specialization of SqliteTvfColumnWriter for fixed-size static row tuples.
 * @tparam N Fixed column count in the tuple.
 */
template <size_t N, typename Enable>
struct SqliteTvfColumnWriter<SqliteValueTuple<N, Enable>> {
    /**
     * @brief Writes the specific column value at `col_idx` from the static tuple.
     * @param ctx The SQLite output context.
     * @param row The static tuple.
     * @param col_idx 0-based column index requested by SQLite.
     */
    static inline void write(SqliteContext ctx, const SqliteValueTuple<N, Enable>& row, int col_idx) {
        if (col_idx >= 0 && col_idx < static_cast<int>(N)) {
            row[col_idx].result(ctx.get());
        }
    }
};

/**
 * @brief Specialization of SqliteTvfColumnWriter for dynamic multi-column value vectors.
 */
template <size_t N, typename Enable>
struct SqliteTvfColumnWriter<SqliteValueVec<N, Enable>> {
    /**
     * @brief Writes the specific column value at `col_idx` from the dynamic vector container.
     * @param ctx The SQLite output context.
     * @param row The dynamic vector container.
     * @param col_idx 0-based column index requested by SQLite.
     */
    static inline void write(SqliteContext ctx, const SqliteValueVec<N, Enable>& row, int col_idx) {
        if (col_idx >= 0 && col_idx < row.size()) {
            row[col_idx].result(ctx.get());
        }
    }
};

// ============================================================================
// COROUTINE TVF MODULE IMPLEMENTATION
// ============================================================================

/**
 * @struct SqliteTvfCoroModule
 * @brief Internal SQLite C Virtual Table module bridging coroutine generators to the VDBE engine.
 *
 * Implements the full SQLite eponymous virtual table callback table (`sqlite3_module`),
 * managing the lifecycle of generator instances across query preparation and execution.
 *
 * @tparam T User struct defining `static const char* schema()` and `static auto generate(SqliteUdfArgs)`.
 */
template <typename T>
struct SqliteTvfCoroModule {
    /** @brief Deduced generator return type from T::generate. */
    typedef decltype(T::generate(SqliteUdfArgs(0, nullptr))) GeneratorType;

    /** @brief Deduced raw value type yielded by the generator, stripped of const/ref qualifiers. */
    typedef typename sqlite_remove_cv<decltype(sqlite_declval<GeneratorType>().value())>::type ValueType;

    /**
     * @struct VTab
     * @brief Ephemeral virtual table structure holding database and state references.
     */
    struct VTab {
        sqlite3_vtab base;      /**< SQLite base virtual table structure. */
        void*        raw_state; /**< Shared per-database state pointer from SqliteExtState. */
    };

    /**
     * @struct Cursor
     * @brief Virtual table query cursor holding the active coroutine generator instance.
     */
    struct Cursor {
        sqlite3_vtab_cursor base;   /**< SQLite base cursor structure. */
        GeneratorType*      gen;    /**< Heap-allocated coroutine generator instance. */
        sqlite3_int64       rowid;  /**< Monotonically increasing 1-based row counter. */
    };

    /**
     * @brief SQLite xConnect callback: declares the virtual table schema.
     * @param db SQLite database connection handle.
     * @param pAux Shared state pointer passed during module registration.
     * @param ppVtab Output pointer receiving the allocated VTab structure.
     * @return SQLITE_OK on success, or an SQLite error code.
     */
    static int xConnect(sqlite3* db, void* pAux, int, const char* const*, sqlite3_vtab** ppVtab, char**) {
        int rc = sqlite3_declare_vtab(db, T::schema());
        if (rc == SQLITE_OK) {
            VTab* pTab = sqlite_new<VTab>();
            if (!pTab) return SQLITE_NOMEM;
            pTab->raw_state = pAux;
            *ppVtab = &pTab->base;
        }
        return rc;
    }

    /**
     * @brief SQLite xDisconnect callback: releases the virtual table instance.
     * @param pVtab Pointer to the VTab structure to destroy.
     * @return SQLITE_OK.
     */
    static int xDisconnect(sqlite3_vtab* pVtab) {
        VTab* pTab = reinterpret_cast<VTab*>(pVtab);
        sqlite_delete(pTab);
        return SQLITE_OK;
    }

    /**
     * @brief SQLite xBestIndex callback: maps hidden arguments to input argv constraints.
     * @param pIdxInfo SQLite query planner index info structure.
     * @return SQLITE_OK on success, or SQLITE_CONSTRAINT if unusable.
     */
    static int xBestIndex(sqlite3_vtab*, sqlite3_index_info* pIdxInfo) {
        int usable_constraints = 0;
        for (int i = 0; i < pIdxInfo->nConstraint; i++) {
            if (pIdxInfo->aConstraint[i].op == SQLITE_INDEX_CONSTRAINT_EQ) {
                int col = pIdxInfo->aConstraint[i].iColumn;
                if (col > 0) {
                    if (!pIdxInfo->aConstraint[i].usable) {
                        return SQLITE_CONSTRAINT;
                    }
                    usable_constraints++;
                    pIdxInfo->aConstraintUsage[i].argvIndex = usable_constraints;
                    pIdxInfo->aConstraintUsage[i].omit = 1;
                }
            }
        }
        pIdxInfo->estimatedCost = 10000.0 / (usable_constraints + 1);
        return SQLITE_OK;
    }

    /**
     * @brief SQLite xOpen callback: allocates a new query cursor.
     * @param ppCursor Output pointer receiving the allocated Cursor structure.
     * @return SQLITE_OK on success, or SQLITE_NOMEM.
     */
    static int xOpen(sqlite3_vtab*, sqlite3_vtab_cursor** ppCursor) {
        Cursor* pCur = sqlite_new<Cursor>();
        if (!pCur) return SQLITE_NOMEM;
        pCur->gen = nullptr;
        pCur->rowid = 0;
        *ppCursor = &pCur->base;
        return SQLITE_OK;
    }

    /**
     * @brief SQLite xClose callback: cleans up the cursor and active generator.
     * @param cur Pointer to the cursor to close.
     * @return SQLITE_OK.
     */
    static int xClose(sqlite3_vtab_cursor* cur) {
        Cursor* pCur = reinterpret_cast<Cursor*>(cur);
        if (pCur->gen) {
            sqlite_delete(pCur->gen);
            pCur->gen = nullptr;
        }
        sqlite_delete(pCur);
        return SQLITE_OK;
    }

    /**
     * @brief SQLite xFilter callback: instantiates and initializes the coroutine generator with query arguments.
     * @param cur The active cursor.
     * @param argc Number of passed arguments.
     * @param argv Array of SQLite argument values.
     * @return SQLITE_OK on success, or SQLITE_NOMEM.
     */
    static int xFilter(sqlite3_vtab_cursor* cur, int, const char*, int argc, sqlite3_value** argv) {
        Cursor* pCur = reinterpret_cast<Cursor*>(cur);
        if (pCur->gen) {
            sqlite_delete(pCur->gen);
            pCur->gen = nullptr;
        }

        SqliteUdfArgs args(argc, argv);
        GeneratorType gen_instance = T::generate(args);
        pCur->gen = sqlite_new<GeneratorType>(sqlite_move(gen_instance));
        if (!pCur->gen) return SQLITE_NOMEM;

        pCur->rowid = 1;
        return SQLITE_OK;
    }

    /**
     * @brief SQLite xNext callback: advances the coroutine generator to the next yielded item.
     * @param cur The active cursor.
     * @return SQLITE_OK.
     */
    static int xNext(sqlite3_vtab_cursor* cur) {
        Cursor* pCur = reinterpret_cast<Cursor*>(cur);
        if (pCur->gen) {
            pCur->gen->next();
            pCur->rowid++;
        }
        return SQLITE_OK;
    }

    /**
     * @brief SQLite xEof callback: checks if the coroutine generator has finished producing rows.
     * @param cur The active cursor.
     * @return 1 if EOF reached or uninitialized, 0 if more rows are available.
     */
    static int xEof(sqlite3_vtab_cursor* cur) {
        Cursor* pCur = reinterpret_cast<Cursor*>(cur);
        if (!pCur->gen) return 1;
        return pCur->gen->is_done() ? 1 : 0;
    }

    /**
     * @brief SQLite xColumn callback: extracts the column value from the active generator item.
     * @param cur The active cursor.
     * @param ctx The SQLite context receiving the result value.
     * @param col_idx 0-based column index requested.
     * @return SQLITE_OK.
     */
    static int xColumn(sqlite3_vtab_cursor* cur, sqlite3_context* ctx, int col_idx) {
        Cursor* pCur = reinterpret_cast<Cursor*>(cur);
        if (!pCur->gen || pCur->gen->is_done()) return SQLITE_OK;

        VTab* pTab = reinterpret_cast<VTab*>(cur->pVtab);
        SqliteContext sqlite_ctx(ctx, pTab ? pTab->raw_state : nullptr);

        SqliteTvfColumnWriter<ValueType>::write(sqlite_ctx, pCur->gen->value(), col_idx);
        return SQLITE_OK;
    }

    /**
     * @brief SQLite xRowid callback: returns the current row ID.
     * @param cur The active cursor.
     * @param pRowid Output pointer receiving the 64-bit row ID.
     * @return SQLITE_OK.
     */
    static int xRowid(sqlite3_vtab_cursor* cur, sqlite3_int64* pRowid) {
        Cursor* pCur = reinterpret_cast<Cursor*>(cur);
        *pRowid = pCur->rowid;
        return SQLITE_OK;
    }

    /** @brief Static SQLite virtual table module definition table. */
    static constexpr sqlite3_module module_def = {
        0,
        xConnect,
        xConnect,
        xBestIndex,
        xDisconnect,
        xDisconnect,
        xOpen,
        xClose,
        xFilter,
        xNext,
        xEof,
        xColumn,
        xRowid,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
    };

    /**
     * @brief Registers the coroutine TVF module with the SQLite engine (Stateless).
     * @param db Database connection view.
     * @param name SQL function name.
     * @return SQLITE_OK on success, or an SQLite error code.
     */
    static inline int define(SqliteDatabaseView db, const char* name) {
        return sqlite3_create_module(db.get(), name, &module_def, nullptr);
    }

    /**
     * @brief Registers the coroutine TVF module with shared per-connection database state.
     * @tparam State User state class managed by SqliteExtState.
     * @param db Database connection view.
     * @param name SQL function name.
     * @return SQLITE_OK on success, or an SQLite error code.
     */
    template <typename State>
    static inline int define_with_state(SqliteDatabaseView db, const char* name) {
        void* raw_state = SqliteExtState<State>::init(db.get());
        return sqlite3_create_module_v2(
            db.get(),
            name,
            &module_def,
            raw_state,
            SqliteExtState<State>::destructor
        );
    }
};

template <typename T>
constexpr sqlite3_module SqliteTvfCoroModule<T>::module_def;

// ============================================================================
// PUBLIC REGISTRATION API
// ============================================================================

/**
 * @class SqliteTvfCoro
 * @brief Public interface for registering generator-based Table-Valued Functions.
 *
 * Provides convenient static methods to bind a generator struct into SQLite
 * as an eponymous virtual table function.
 */
class SqliteTvfCoro {
public:
    /**
     * @brief Registers a coroutine generator as a SQLite Table-Valued Function (Stateless).
     *
     * Example:
     * @code
     * struct SeriesTvf {
     *     static constexpr const char* schema() {
     *         return "CREATE TABLE x(value INT, start HIDDEN, stop HIDDEN)";
     *     }
     *     static SqliteFiberGenerator<sqlite3_int64> generate(SqliteUdfArgs args) {
     *         sqlite3_int64 start = args.size() > 0 ? args[0].as_int64() : 0;
     *         sqlite3_int64 stop  = args.size() > 1 ? args[1].as_int64() : 10;
     *         return SqliteFiberGenerator<sqlite3_int64>([=](const auto& yield) {
     *             for (sqlite3_int64 v = start; v <= stop; ++v) yield(v);
     *         });
     *     }
     * };
     * SqliteTvfCoro::define<SeriesTvf>(db, "my_series");
     * @endcode
     *
     * @tparam T Struct providing `schema()` and `generate(SqliteUdfArgs)`.
     * @param db Database connection view (or sqlite3* / SqliteDatabaseOwned).
     * @param name SQL function name (e.g., generate_series).
     * @return SQLITE_OK on success, or an SQLite error code.
     */
    template <typename T>
    static inline int define(SqliteDatabaseView db, const char* name) {
        return SqliteTvfCoroModule<T>::define(db, name);
    }

    /**
     * @brief Registers a coroutine generator with shared per-connection database state injection.
     *
     * Injects the shared `StateType` instance directly into `SqliteContext`, enabling O(1) single-instruction
     * `ctx.state<StateType>()` retrieval inside the generator or companion UDFs.
     *
     * @tparam StateType User state class managed by SqliteExtState.
     * @tparam T Struct providing `schema()` and `generate(SqliteUdfArgs)`.
     * @param db Database connection view (or sqlite3* / SqliteDatabaseOwned).
     * @param name SQL function name.
     * @return SQLITE_OK on success, or an SQLite error code.
     */
    template <typename StateType, typename T>
    static inline int define_with_state(SqliteDatabaseView db, const char* name) {
        return SqliteTvfCoroModule<T>::template define_with_state<StateType>(db, name);
    }
};

#endif /* SQLITE3_TVF_CORO_HPP */
