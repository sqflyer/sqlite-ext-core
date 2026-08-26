#ifndef SQLITE3_ROW_HPP
#define SQLITE3_ROW_HPP

#include <sqlite3.h>
#include "sqlite3_value.hpp"
#include "sqlite3_allocator.hpp"

// Forward declaration of SqliteStatement for tight integration
class SqliteStatement;
class SqliteRowDynamic;

// ============================================================================
// Source Type Macros for SqliteRowView
// ============================================================================
#define SQLITE_ROW_SOURCE_STMT        0  /**< Backed by sqlite3_stmt* column values */
#define SQLITE_ROW_SOURCE_ARGV        1  /**< Backed by sqlite3_value** (UDF args / vtab) */
#define SQLITE_ROW_SOURCE_OWNED_ARRAY 2  /**< Backed by const SqliteValueOwned* array */
#define SQLITE_ROW_SOURCE_VIEW_ARRAY  3  /**< Backed by const SqliteValueView* array */
#define SQLITE_ROW_SOURCE_EMPTY       4  /**< Empty row view (0 columns) */

typedef uint8_t SqliteRowSourceType;

// ============================================================================
// 1. SqliteRowView: Universal Non-Owning Multi-Column Row View
// ============================================================================

/**
 * @brief Lightweight, zero-allocation non-owning view over a multi-column SQLite row.
 * 
 * `SqliteRowView` provides uniform, zero-copy, bounds-checked access to multi-column
 * tabular rows. It acts as a universal abstraction layer over:
 * - Active prepared statement rows (`sqlite3_stmt*` or `SqliteStatement&`)
 * - SQLite UDF / aggregate argument vectors (`SqliteUdfArgs` or `sqlite3_value**`)
 * - In-memory contiguous owned value arrays (`const SqliteValueOwned*` or `SqliteRowStatic<N>`)
 * - In-memory contiguous view arrays (`const SqliteValueView*`)
 * 
 * All column access operations (`operator[]`, `as_text()`, `as_int64()`, etc.) return
 * lightweight non-owning views with zero heap allocations.
 */
class SqliteRowView {
private:
    union {
        SqliteStatement*        m_stmt;
        SqliteValueViewArray    m_argv;
        const SqliteValueOwned* m_owned_array;
        const SqliteValueView*  m_view_array;
    };
    int                         m_col_count;
    SqliteRowSourceType         m_source; // SQLITE_ROW_SOURCE_*

public:
    /**
     * @brief Constructs an empty row view with zero columns.
     */
    inline SqliteRowView() noexcept : m_stmt(nullptr), m_col_count(0), m_source(SQLITE_ROW_SOURCE_EMPTY) {}

    /**
     * @brief Constructs a row view wrapping an active prepared statement step row.
     * 
     * @param stmt The raw SQLite statement handle (sqlite3_stmt*) positioned at SQLITE_ROW.
     */
    inline explicit SqliteRowView(sqlite3_stmt* stmt) noexcept
        : m_stmt(reinterpret_cast<SqliteStatement*>(stmt)), m_col_count(stmt ? sqlite3_column_count(stmt) : 0), m_source(SQLITE_ROW_SOURCE_STMT) {}

    /**
     * @brief Constructs a row view wrapping an active SqliteStatement pointer positioned at SQLITE_ROW.
     * 
     * @param stmt Pointer to SqliteStatement wrapper.
     */
    inline explicit SqliteRowView(SqliteStatement* stmt) noexcept
        : m_stmt(stmt), m_col_count(stmt ? sqlite3_column_count(reinterpret_cast<sqlite3_stmt*>(stmt)) : 0), m_source(SQLITE_ROW_SOURCE_STMT) {}

    /**
     * @brief Constructs a row view wrapping raw UDF / vtab argument arrays.
     * 
     * @param argc Number of arguments in the array.
     * @param argv Array of sqlite3_value* pointers.
     */
    inline SqliteRowView(int argc, sqlite3_value** argv) noexcept
        : m_argv(argc, argv), m_col_count(argc >= 0 ? argc : 0), m_source(SQLITE_ROW_SOURCE_ARGV) {}

    /**
     * @brief Constructs a row view wrapping SqliteValueViewArray / SqliteUdfArgs from a UDF callback.
     * 
     * @param args The SqliteValueViewArray instance received in the callback.
     */
    inline SqliteRowView(const SqliteValueViewArray& args) noexcept
        : m_argv(args), m_col_count(args.size()), m_source(SQLITE_ROW_SOURCE_ARGV) {}

    /**
     * @brief Constructs a row view wrapping a contiguous array of SqliteValueOwned.
     * 
     * @param array Pointer to the first SqliteValueOwned element.
     * @param count Number of columns in the array.
     */
    inline SqliteRowView(const SqliteValueOwned* array, int count) noexcept
        : m_owned_array(array), m_col_count(count >= 0 ? count : 0), m_source(SQLITE_ROW_SOURCE_OWNED_ARRAY) {}

    /**
     * @brief Constructs a row view wrapping a contiguous array of SqliteValueView.
     * 
     * @param array Pointer to the first SqliteValueView element.
     * @param count Number of column views in the array.
     */
    inline SqliteRowView(const SqliteValueView* array, int count) noexcept
        : m_view_array(array), m_col_count(count >= 0 ? count : 0), m_source(SQLITE_ROW_SOURCE_VIEW_ARRAY) {}

    /**
     * @brief Returns the total number of columns available in this row.
     * 
     * @return Column count as an integer.
     */
    inline int size() const noexcept { return m_col_count; }

    /**
     * @brief Alias for size() returning the total column count.
     * 
     * @return Column count as an integer.
     */
    inline int column_count() const noexcept { return m_col_count; }

    /**
     * @brief Checks if the row contains zero columns.
     * 
     * @return True if empty (count == 0), false otherwise.
     */
    inline bool empty() const noexcept { return m_col_count == 0; }

    /**
     * @brief Safely accesses a column as a zero-allocation SqliteValueView.
     * 
     * If the column index is out of bounds (< 0 or >= size()), returns a SQLITE_NULL view
     * to guarantee complete segfault immunity. Note: For viewing owned value arrays (source 2),
     * prefer using direct typed accessors (as_text, as_int64, etc.) or .to_owned().
     * 
     * @param col 0-indexed column index.
     * @return Non-owning SqliteValueView for the column.
     */
    inline SqliteValueView operator[](int col) const noexcept {
        if (col < 0 || col >= m_col_count) {
            return SqliteValueView(nullptr);
        }
        switch (m_source) {
            case SQLITE_ROW_SOURCE_STMT:        return SqliteValueView::from_column(raw_stmt(), col);
            case SQLITE_ROW_SOURCE_ARGV:        return m_argv[col];
            case SQLITE_ROW_SOURCE_VIEW_ARRAY:  return m_view_array[col];
            default:                            return SqliteValueView(nullptr);
        }
    }

    /**
     * @brief Bounds-safe column accessor identical to operator[].
     * 
     * @param col 0-indexed column index.
     * @return Non-owning SqliteValueView for the column.
     */
    inline SqliteValueView at(int col) const noexcept { return (*this)[col]; }

    /**
     * @brief Extracts a column value as a zero-allocation SqliteValueView.
     * 
     * Enables fluent typed extraction: row.get_column(0).as_double(), row.get_column(1).as_text(), etc.
     * 
     * @param col 0-indexed column index.
     * @return Non-owning SqliteValueView for the column.
     */
    inline SqliteValueView get_column(int col) const noexcept { return (*this)[col]; }

    /**
     * @brief Alias for get_column().
     * 
     * @param col 0-indexed column index.
     * @return Non-owning SqliteValueView for the column.
     */
    inline SqliteValueView column(int col) const noexcept { return (*this)[col]; }

    /**
     * @brief Returns the column name (only available when backed by a prepared statement).
     * 
     * @param col 0-indexed column index.
     * @return UTF-8 column name string, or nullptr if unavailable / out of bounds.
     */
    inline const char* column_name(int col) const noexcept {
        if (m_source == SQLITE_ROW_SOURCE_STMT && m_stmt && col >= 0 && col < m_col_count) {
            return sqlite3_column_name(raw_stmt(), col);
        }
        return nullptr;
    }

    /**
     * @brief Returns the declared column datatype in the table schema (e.g. "INTEGER", "TEXT").
     * 
     * Only available when backed by a prepared statement.
     * 
     * @param col 0-indexed column index.
     * @return Declared column type string, or nullptr if unavailable / out of bounds.
     */
    inline const char* column_decltype(int col) const noexcept {
        if (m_source == SQLITE_ROW_SOURCE_STMT && m_stmt && col >= 0 && col < m_col_count) {
            return sqlite3_column_decltype(raw_stmt(), col);
        }
        return nullptr;
    }

    // =========================================================================
    // Direct Typed Column Accessors
    // =========================================================================

    /**
     * @brief Extracts column value as a 64-bit integer.
     * 
     * @param col 0-indexed column index.
     * @return 64-bit integer value (or 0 if NULL / invalid).
     */
    inline sqlite3_int64 as_int64(int col) const noexcept {
        if (col < 0 || col >= m_col_count) return 0;
        if (m_source == SQLITE_ROW_SOURCE_OWNED_ARRAY) return m_owned_array[col].as_int64();
        return get_column(col).as_int64();
    }

    /**
     * @brief Extracts column value as a double-precision floating point number.
     * 
     * @param col 0-indexed column index.
     * @return Double value (or 0.0 if NULL / invalid).
     */
    inline double as_double(int col) const noexcept {
        if (col < 0 || col >= m_col_count) return 0.0;
        if (m_source == SQLITE_ROW_SOURCE_OWNED_ARRAY) return m_owned_array[col].as_double();
        return get_column(col).as_double();
    }

    /**
     * @brief Extracts column value as a zero-allocation SqliteStringView.
     * 
     * @param col 0-indexed column index.
     * @return String view pointing to the underlying SQLite memory buffer.
     */
    inline SqliteStringView as_text(int col) const noexcept {
        if (col < 0 || col >= m_col_count) return SqliteStringView(nullptr, 0);
        if (m_source == SQLITE_ROW_SOURCE_OWNED_ARRAY) return m_owned_array[col].as_text();
        return get_column(col).as_text();
    }

    /**
     * @brief Extracts column value as a zero-allocation SqliteBlobView.
     * 
     * @param col 0-indexed column index.
     * @return Blob view pointing to the underlying SQLite binary memory buffer.
     */
    inline SqliteBlobView as_blob(int col) const noexcept {
        if (col < 0 || col >= m_col_count) return SqliteBlobView(nullptr, 0);
        if (m_source == SQLITE_ROW_SOURCE_OWNED_ARRAY) return m_owned_array[col].as_blob();
        return get_column(col).as_blob();
    }

    /**
     * @brief Evaluates column value as a boolean (non-zero integer is true).
     * 
     * @param col 0-indexed column index.
     * @return True if non-zero, false otherwise.
     */
    inline bool as_bool(int col) const noexcept {
        if (col < 0 || col >= m_col_count) return false;
        if (m_source == SQLITE_ROW_SOURCE_OWNED_ARRAY) return m_owned_array[col].as_bool();
        return get_column(col).as_bool();
    }

    /**
     * @brief Checks if the specified column is SQLITE_NULL.
     * 
     * @param col 0-indexed column index.
     * @return True if NULL or out of bounds, false otherwise.
     */
    inline bool is_null(int col) const noexcept {
        return type(col) == SQLITE_NULL;
    }

    /**
     * @brief Returns the SQLite fundamental datatype (SQLITE_INTEGER, SQLITE_FLOAT, SQLITE_TEXT, SQLITE_BLOB, SQLITE_NULL).
     * 
     * @param col 0-indexed column index.
     * @return SQLite datatype constant.
     */
    inline int type(int col) const noexcept {
        if (col < 0 || col >= m_col_count) return SQLITE_NULL;
        if (m_source == SQLITE_ROW_SOURCE_OWNED_ARRAY) return m_owned_array[col].type();
        return get_column(col).type();
    }

    /**
     * @brief Returns the 8-bit SQLite subtype for the column (e.g. 'J' for JSON, 'V' for Vector).
     * 
     * @param col 0-indexed column index.
     * @return 8-bit subtype value (0 if none).
     */
    inline uint8_t subtype(int col) const noexcept {
        if (col < 0 || col >= m_col_count) return SQLITE_SUBTYPE_NONE;
        if (m_source == SQLITE_ROW_SOURCE_OWNED_ARRAY) return m_owned_array[col].subtype();
        return get_column(col).subtype();
    }

    /** @brief Returns internal source type (SQLITE_ROW_SOURCE_*). */
    inline SqliteRowSourceType source_type() const noexcept { return m_source; }
    inline sqlite3_stmt* raw_stmt() const noexcept { return reinterpret_cast<sqlite3_stmt*>(m_stmt); }
    inline SqliteStatement* statement() const noexcept { return m_stmt; }
    inline sqlite3_value** raw_argv() const noexcept { return m_argv.argv(); }
    inline SqliteValueViewArray argv() const noexcept { return m_argv; }
    inline const SqliteValueOwned* raw_owned_array() const noexcept { return m_owned_array; }
    inline const SqliteValueView* raw_view_array() const noexcept { return m_view_array; }

    /**
     * @brief Materializes this transient non-owning row into an owned SqliteRowDynamic snapshot.
     * 
     * Deep copies all column strings, blobs, and subtypes into managed memory.
     * 
     * @return SqliteRowDynamic containing owned snapshots of all columns.
     */
    inline SqliteRowDynamic to_owned() const;

    // =========================================================================
    // Range-Based For Loop Iterator
    // =========================================================================

    /**
     * @brief Forward iterator for traversing columns in a range-based for loop.
     * 
     * Example:
     * @code
     * for (SqliteValueView col : row) {
     *     printf("Col type: %d\n", col.type());
     * }
     * @endcode
     */
    class Iterator {
    private:
        const SqliteRowView* m_row;
        int                  m_idx;
    public:
        inline Iterator(const SqliteRowView* row, int idx) noexcept : m_row(row), m_idx(idx) {}
        inline SqliteValueView operator*() const noexcept { return (*m_row)[m_idx]; }
        inline Iterator& operator++() noexcept { ++m_idx; return *this; }
        inline Iterator operator++(int) noexcept { Iterator tmp = *this; ++m_idx; return tmp; }
        inline bool operator==(const Iterator& other) const noexcept { return m_idx == other.m_idx && m_row == other.m_row; }
        inline bool operator!=(const Iterator& other) const noexcept { return !(*this == other); }
    };

    /** @brief Returns an iterator to the first column. */
    inline Iterator begin() const noexcept { return Iterator(this, 0); }

    /** @brief Returns an iterator to the end of the columns. */
    inline Iterator end() const noexcept { return Iterator(this, m_col_count); }
};

// ============================================================================
// 1.5. SqliteRowUtil: Shared Row Construction Utilities
// ============================================================================
namespace SqliteRowUtil {
    /**
     * @brief Copies up to `count` columns from a SqliteRowView into a contiguous
     *        SqliteValueOwned destination buffer.
     * 
     * Handles both owned-array sources (via clone()) and all other sources
     * (statement columns, argv, view arrays) via SqliteValueView::to_owned().
     * 
     * @param dest  Pointer to the destination SqliteValueOwned buffer.
     * @param view  The non-owning source row view.
     * @param count Number of elements to copy.
     */
    inline void copy_from_view(SqliteValueOwned* dest, const SqliteRowView& view, int count) noexcept {
        if (view.source_type() == SQLITE_ROW_SOURCE_OWNED_ARRAY) {
            const SqliteValueOwned* src = view.raw_owned_array();
            for (int i = 0; i < count; ++i) {
                dest[i] = src[i].clone();
            }
        } else {
            for (int i = 0; i < count; ++i) {
                dest[i] = view.get_column(i).to_owned();
            }
        }
    }
} // namespace SqliteRowUtil

// ============================================================================
// 2. SqliteRowStatic<size_t N>: Pure Stack-Allocated Row (0 Mallocs)
// ============================================================================

/**
 * @brief Fixed-size, compile-time stack-allocated row container storing exactly N columns.
 * 
 * Inherits from `SqliteValueOwnedStaticArray<N>` with added tabular row methods.
 * Footprint: Exactly N * 16 Bytes on the stack or in-situ within a data structure.
 * Guaranteed 0 heap allocations for maximum cache density and execution speed.
 * 
 * Perfect for virtual table engines (e.g. memkv_lru, memkv_map) with fixed-column schemas.
 * 
 * @tparam N The number of columns in the row (must be > 0).
 */
template <size_t N>
class SqliteRowStatic : public SqliteValueOwnedStaticArray<N> {
public:
    /**
     * @brief Initializes all N columns to SQLITE_NULL.
     */
    inline SqliteRowStatic() noexcept : SqliteValueOwnedStaticArray<N>() {}

    /**
     * @brief Copies and materializes up to N columns from a non-owning row view.
     * 
     * @param view The SqliteRowView to snapshot.
     */
    inline explicit SqliteRowStatic(const SqliteRowView& view) noexcept {
        int limit = static_cast<int>(N);
        if (view.size() < limit) limit = view.size();
        SqliteRowUtil::copy_from_view(this->m_values, view, limit);
    }

    /**
     * @brief Alias for size() returning the compile-time column count.
     * 
     * @return Integer column count.
     */
    inline int column_count() const noexcept { return static_cast<int>(N); }

    /**
     * @brief Converts this stack row into a zero-allocation non-owning SqliteRowView.
     * 
     * @return SqliteRowView wrapping the stack array.
     */
    inline SqliteRowView view() const noexcept {
        return SqliteRowView(this->data(), static_cast<int>(N));
    }

    /** @brief Implicit conversion operator to SqliteRowView. */
    inline operator SqliteRowView() const noexcept {
        return view();
    }
};

// ============================================================================
// 3. SqliteRowDynamic: Runtime-Sized Heap Row Container (sqlite3_malloc64)
// ============================================================================

/**
 * @brief Dynamic, heap-allocated row container for queries with runtime column counts.
 * 
 * Inherits from `SqliteValueOwnedDynamicArray` with added tabular row methods.
 * Allocates a contiguous buffer of SqliteValueOwned using SQLite's native memory
 * allocator (sqlite3_malloc64 / sqlite3_free). Integrates zero-dependency move semantics
 * and placement new/delete via sqlite3_allocator.hpp.
 */
class SqliteRowDynamic : public SqliteValueOwnedDynamicArray {
public:
    /**
     * @brief Constructs an empty dynamic row.
     */
    inline SqliteRowDynamic() noexcept : SqliteValueOwnedDynamicArray() {}

    /**
     * @brief Constructs a dynamic row with pre-allocated column count, initialized to SQLITE_NULL.
     * 
     * @param count Number of columns to allocate.
     */
    inline explicit SqliteRowDynamic(int count) : SqliteValueOwnedDynamicArray(count) {}

    /**
     * @brief Constructs and materializes a dynamic row snapshot from a row view.
     * 
     * @param view Non-owning row view to copy.
     */
    inline SqliteRowDynamic(const SqliteRowView& view) : SqliteValueOwnedDynamicArray() {
        int sz = view.size();
        if (sz > 0) {
            this->resize(sz);
            SqliteRowUtil::copy_from_view(this->m_values, view, sz);
        }
    }

    /** @brief Alias for size() returning the number of columns. */
    inline int column_count() const noexcept { return this->size(); }

    /**
     * @brief Converts this heap row into a zero-allocation non-owning SqliteRowView.
     * 
     * @return SqliteRowView wrapping the heap array.
     */
    inline SqliteRowView view() const noexcept {
        return SqliteRowView(this->data(), this->size());
    }

    /** @brief Implicit conversion operator to SqliteRowView. */
    inline operator SqliteRowView() const noexcept {
        return view();
    }
};

// ============================================================================
// 4. SqliteRowOwned<size_t N>: Unified Row Template
// ============================================================================

/**
 * @brief Unified Row template alias.
 * - SqliteRowOwned<N> (N > 0): Stack-allocated SqliteRowStatic<N> (0 heap allocations).
 * - SqliteRowOwned<0>: Dynamic heap-allocated SqliteRowDynamic (runtime sizing).
 */
template <size_t N = 0>
class SqliteRowOwned : public SqliteRowStatic<N> {
public:
    using SqliteRowStatic<N>::SqliteRowStatic;
};

/**
 * @brief Specialization of SqliteRowOwned for N=0 mapping to SqliteRowDynamic.
 */
template <>
class SqliteRowOwned<0> : public SqliteRowDynamic {
public:
    using SqliteRowDynamic::SqliteRowDynamic;
};

// ============================================================================
// 5. Inlined Implementations
// ============================================================================

inline SqliteRowDynamic SqliteRowView::to_owned() const {
    return SqliteRowDynamic(*this);
}

#endif // SQLITE3_ROW_HPP
